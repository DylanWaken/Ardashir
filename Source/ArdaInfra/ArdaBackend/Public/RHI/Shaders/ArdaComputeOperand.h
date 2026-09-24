/** @file ArdaComputeOperand.h
 * Single-operation operands with user binding/selection and framework-owned dispatch.
 */
#pragma once
#include "RHI/CUDA/ArdaCudaKernelVariants.h"
#include "RHI/Context/ArdaCudaSelectionContext.h"
#include "RHI/Scheduling/ArdaCudaSubmission.h"
#include <mutex>
#include <exception>

namespace arda
{
	/** Common diagnostic and resource metadata interface. */
	class FArdaComputeOperand
	{
	public:
		virtual ~FArdaComputeOperand() = default;
		virtual const char* GetName() const noexcept = 0;
		/** Checks required features for at least one supplied implementation without preparing or submitting GPU work. */
		virtual FArdaRHIStatus CheckRequiredFeatures() const = 0;
		/** Equivalent to CheckRequiredFeatures(). */
		virtual FArdaRHIStatus GetOperandSupport() const = 0;
		virtual const FArdaComputeParameterMetadata& GetParameterMetadata() const = 0;
	};

	/** Authors supply CUDA binding/selection and, optionally, an explicit compute shader alternative. Dispatch cannot be overridden.
     * Binding is synchronized once per operand. Selection must have no GPU/host side effects.
     * Each dispatch retains its own frozen values/resources independently of the operand.
     */
	template <class ParameterType, class VariantPayload>
	class TArdaComputeOperand : public FArdaComputeOperand
	{
	public:
		using FArdaParameters = ParameterType;
		using FArdaKernelParameters = typename ParameterType::FArdaCuda;
		using FArdaRegistry = TArdaCudaKernelRegistry<ParameterType, VariantPayload>;
		using FArdaVariants = typename FArdaRegistry::FArdaVariants;
		using FArdaPlan = eastl::shared_ptr<const FArdaCudaDispatchPlan>;

		explicit TArdaComputeOperand(FArdaRHIDeviceRef Device)
		    : mDevice(eastl::move(Device))
		{
		}

		const FArdaComputeParameterMetadata& GetParameterMetadata() const final
		{
			return ParameterType::GetStaticMetadata();
		}

		/** Registers compiled symbols or external library factories and payloads without calling CUDA. */
		virtual void BindKernelVariants(FArdaRegistry& Registry) const = 0;

		/** Chooses one compatible operation using host metadata; can fail or explicitly return NoWork. */
		virtual TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FArdaParameters& Parameters,
		    const FArdaCudaSelectionContext& Context,
		    const FArdaVariants& Candidates) const = 0;

		/** Declares an authored compute alternative, permitting an empty CUDA registry in builds without native kernels. */
		virtual bool HasComputeAlternative() const noexcept
		{
			return false;
		}

		/** Checks the alternative's feature requirements without allocation, recording, or submission.
		 * Override together with HasComputeAlternative and PrepareComputeAlternative; default is Unsupported.
		 */
		virtual FArdaRHIStatus CheckComputeAlternativeSupport() const
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No supported compute alternative was supplied.");
		}

		/** Freezes shader resources, shader-specific constants, and dispatch dimensions for the supplied parameters.
		 * Called only after feature preflight selects the alternative. Must not record or submit GPU work.
		 */
		virtual TArdaRHIResult<FArdaComputeAlternativePlan> PrepareComputeAlternative(const FArdaParameters&,
		    EArdaRHIQueueType) const
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No compute alternative was supplied.")};
		}

		/** Initializes and returns a read-only registry with operand lifetime. */
		TArdaRHIResult<const FArdaRegistry*> GetKernelVariants() const
		{
			auto S = EnsureBindings();
			return {S ? &mRegistry : nullptr, S};
		}

		/** Validates schema, immutable registry, and feature support for a supplied CUDA or compute implementation.
         * Does not validate a particular resource allocation/view or launch a kernel.
         * @errors Returns the first binding error, InvalidArgument for no device, or Unsupported for missing mode/native coverage.
         * @threading Safe for concurrent inspection; binding initializes once without calling CUDA.
         */
		FArdaRHIStatus CheckRequiredFeatures() const final
		{
			return SelectDispatchRoute().mStatus;
		}

		/** Equivalent to CheckRequiredFeatures(); retained for existing operand callers. */
		FArdaRHIStatus GetOperandSupport() const final
		{
			return CheckRequiredFeatures();
		}

		/** Selects the implementation from immutable host metadata for graph lowering before resource allocation.
		 * Prefers compatible CUDA; only Unsupported feature results may select an explicit compute alternative.
		 * Schema/binding errors are never hidden. The alternative is not retried after preparation or execution fails.
		 */
		TArdaRHIResult<EArdaComputeDispatchRoute> SelectDispatchRoute(
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) const
		{
			FArdaCudaSelectionContext Context{{}, Queue};
			FArdaVariants Candidates;
			return SelectDispatchRoute(Context, Candidates);
		}

		/** Freezes a validated launch plan. Parameters can be changed/destroyed after return. */
		virtual TArdaRHIResult<FArdaPlan> PrepareDispatch(const FArdaParameters& Parameters,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) const final
		{
			FArdaCudaSelectionContext Context{{}, Queue};
			FArdaVariants Candidates;
			const auto Route = SelectDispatchRoute(Context, Candidates);
			if (!Route)
			{
				return {{}, Route.mStatus};
			}
			auto Plan = eastl::make_shared<FArdaCudaDispatchPlan>();
			Plan->mDevice = mDevice;
			Plan->mQueue = Queue;
			Plan->mRoute = Route.mValue;
			if (Route.mValue == EArdaComputeDispatchRoute::ComputeAlternative)
			{
				TArdaRHIResult<FArdaComputeAlternativePlan> Alternative;
				try
				{
					Alternative = PrepareComputeAlternative(Parameters, Queue);
				}
				catch (const std::exception& E)
				{
					return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, E.what())};
				}
				catch (...)
				{
					return {{},
					    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					        "Compute alternative preparation threw an exception.")};
				}
				if (!Alternative)
				{
					return {{}, Alternative.mStatus};
				}
				if (auto Status = ValidateComputeAlternative(Alternative.mValue); !Status)
				{
					return {{}, Status};
				}
				Plan->mSelection.mbNoWork = Alternative.mValue.mbNoWork;
				Plan->mComputeAlternative = eastl::move(Alternative.mValue);
				return {Plan, {}};
			}
			if (auto S = ParameterType::GetCudaMetadata().Prepare(&Parameters, Plan->mDispatch); !S)
			{
				return {{}, S};
			}
			TArdaRHIResult<FArdaCudaKernelSelection> Choice;
			try
			{
				Choice = SelectKernel(Parameters, Context, Candidates);
			}
			catch (const std::exception& E)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, E.what())};
			}
			catch (...)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				        "CUDA kernel selection threw an exception.")};
			}
			if (!Choice)
			{
				return {{}, Choice.mStatus};
			}
			Plan->mSelection = Choice.mValue;
			if (Choice.mValue.mbNoWork)
			{
				Plan->mDispatch = {};
				return {Plan, {}};
			}
			auto& K = Plan->mDispatch.mKernels.front();
			for (const auto& V : Candidates)
			{
				if (V.mId == Choice.mValue.mVariantId)
				{
					K.mEntry = V.mEntry;
					K.mExternalCall = V.mExternalCall;
					break;
				}
			}
			if (!K.mEntry && !K.mExternalCall)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				        "SelectKernel returned an unregistered or incompatible variant.")};
			}
			static_cast<FArdaCudaLaunchConfig&>(K) = Choice.mValue.mLaunch;
			if (auto S = ValidateArdaCudaKernels(Plan->mDispatch.mKernels,
			        Plan->mDispatch.mBindings.size(),
			        Context.mCapabilities);
			    !S)
			{
				return {{}, S};
			}
			return {Plan, {}};
		}

		/** Records a frozen plan without submitting. Native addresses resolve in the provider. */
		static FArdaRHIStatus RecordPlan(IArdaRHICommandList& Commands, const FArdaPlan& Plan)
		{
			if (!Plan || Commands.GetDevice() != Plan->mDevice.Get())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "CUDA plan belongs to another device.");
			}
			if (Commands.GetQueueType() != Plan->mQueue)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CUDA plan belongs to another queue type.");
			}
			if (Plan->mRoute != EArdaComputeDispatchRoute::Cuda &&
			    Plan->mRoute != EArdaComputeDispatchRoute::ComputeAlternative)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Unknown compute operand route.");
			}
			if (Plan->mSelection.mbNoWork)
			{
				return {};
			}
			if (Plan->mRoute == EArdaComputeDispatchRoute::ComputeAlternative)
			{
				const auto& Alternative = Plan->mComputeAlternative;
				if (auto Status = Commands.SetComputeState(Alternative.mState); !Status)
				{
					return Status;
				}
				if (!Alternative.mPushConstants.empty())
				{
					Commands.SetPushConstants(Alternative.mPushConstants.data(), Alternative.mPushConstants.size());
				}
				Commands.Dispatch(Alternative.mGroupsX, Alternative.mGroupsY, Alternative.mGroupsZ);
				return {};
			}
			return Commands.DispatchCuda(Plan->mDispatch);
		}

		/** Records one selected kernel or library operation into the caller-owned list; RDG uses this path. */
		virtual FArdaRHIStatus DispatchDeferred(IArdaRHICommandList& Commands,
		    const FArdaParameters& Parameters) const final
		{
			auto Plan = PrepareDispatch(Parameters, Commands.GetQueueType());
			return Plan ? RecordPlan(Commands, Plan.mValue) : Plan.mStatus;
		}

		/** Submits the shared plan path and returns its queue completion identity. */
		virtual TArdaRHIResult<FArdaCudaSubmission> Dispatch(const FArdaParameters& Parameters,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) const final
		{
			auto Plan = PrepareDispatch(Parameters, Queue);
			if (!Plan)
			{
				return {{}, Plan.mStatus};
			}
			FArdaCudaSubmission Result{mDevice, Queue, 0, Plan.mValue->mSelection, Plan.mValue->mRoute};
			if (Plan.mValue->mSelection.mbNoWork)
			{
				return {Result, {}};
			}
			auto Commands = mDevice->CreateCommandList(Queue);
			if (!Commands)
			{
				return {{}, Commands.mStatus};
			}
			if (auto S = Commands.mValue->Open(); !S)
			{
				return {{}, S};
			}
			if (auto S = RecordPlan(*Commands.mValue, Plan.mValue); !S)
			{
				return {{}, S};
			}
			if (auto S = Commands.mValue->Close(); !S)
			{
				return {{}, S};
			}
			auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
			if (!Submitted)
			{
				return {{}, Submitted.mStatus};
			}
			Result.mInstance = Submitted.mValue;
			return {Result, {}};
		}

		const FArdaRHIDeviceRef& GetDevice() const noexcept
		{
			return mDevice;
		}

	private:
		TArdaRHIResult<EArdaComputeDispatchRoute> SelectDispatchRoute(FArdaCudaSelectionContext& Context,
		    FArdaVariants& Candidates) const
		{
			const auto CudaStatus = GetSelectionCandidates(Context, Candidates);
			if (CudaStatus)
			{
				return {EArdaComputeDispatchRoute::Cuda, {}};
			}
			if (CudaStatus.mCode != EArdaRHIResult::Unsupported)
			{
				return {{}, CudaStatus};
			}
			if (!HasComputeAlternative())
			{
				auto Status = CudaStatus;
				Status.mMessage += " No compute alternative was supplied.";
				return {{}, Status};
			}
			if ((Context.mQueue != EArdaRHIQueueType::Graphics && Context.mQueue != EArdaRHIQueueType::Compute) ||
			    !mDevice->GetCapabilities().IsQueueSupported(Context.mQueue))
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				        "Compute alternative requires an available graphics or compute queue.")};
			}
			FArdaRHIStatus AlternativeStatus;
			try
			{
				AlternativeStatus = CheckComputeAlternativeSupport();
			}
			catch (const std::exception& E)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, E.what())};
			}
			catch (...)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				        "Compute alternative feature check threw an exception.")};
			}
			if (!AlternativeStatus)
			{
				if (AlternativeStatus.mCode == EArdaRHIResult::Unsupported)
				{
					AlternativeStatus.mMessage =
					    "CUDA: " + CudaStatus.mMessage + "; compute alternative: " + AlternativeStatus.mMessage;
				}
				return {{}, AlternativeStatus};
			}
			return {EArdaComputeDispatchRoute::ComputeAlternative, {}};
		}

		FArdaRHIStatus ValidateComputeAlternative(const FArdaComputeAlternativePlan& Plan) const
		{
			if (Plan.mbNoWork)
			{
				return {};
			}
			if (!Plan.mState.mPipeline)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Compute alternative requires a compute pipeline or explicit NoWork.");
			}
			const uint32_t Groups[] = {Plan.mGroupsX, Plan.mGroupsY, Plan.mGroupsZ};
			const auto& Limits = mDevice->GetCapabilities().mLimits;
			for (size_t Axis = 0; Axis != 3; ++Axis)
			{
				if (!Groups[Axis] || Groups[Axis] > Limits.mMaxComputeWorkGroupCount[Axis])
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Compute alternative work-group counts exceed reported limits; use explicit NoWork for empty work.");
				}
			}
			if (!Plan.mPushConstants.empty())
			{
				size_t Capacity = SIZE_MAX;
				for (const auto& Layout : Plan.mState.mPipeline->GetDesc().mBindingLayouts)
				{
					if (!Layout)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Compute alternative pipeline has a null binding layout.");
					}
					for (const auto& Item : Layout->GetDesc().mItems)
					{
						if (Item.mType == EArdaRHIBindingType::PushConstants && Item.mArraySize < Capacity)
						{
							Capacity = Item.mArraySize;
						}
					}
				}
				if (Capacity == SIZE_MAX || Plan.mPushConstants.size() > Capacity ||
				    Plan.mPushConstants.size() % sizeof(uint32_t))
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Compute alternative constants must be DWORD-aligned and fit every pipeline push block.");
				}
			}
			return {};
		}

		FArdaRHIStatus GetSelectionCandidates(FArdaCudaSelectionContext& Context, FArdaVariants& Candidates) const
		{
			if (!mDevice)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Compute operand has no device.");
			}
			if (auto S = EnsureBindings(); !S)
			{
				return S;
			}
			Context.mCapabilities = mDevice->GetCudaCapabilities();
			if (!Context.mCapabilities)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    Context.mCapabilities.mUnavailableReason.empty()
				        ? "CUDA is unavailable on this device."
				        : Context.mCapabilities.mUnavailableReason.c_str());
			}
			FArdaRHIStatus CandidateStatus;
			Candidates = mRegistry.GetCompatibleVariants(Context.mCapabilities, &CandidateStatus);
			if (!CandidateStatus)
			{
				return CandidateStatus;
			}
			if (Candidates.empty())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "No bound CUDA variant has compatible native code and execution requirements.");
			}
			if ((Context.mQueue != EArdaRHIQueueType::Graphics && Context.mQueue != EArdaRHIQueueType::Compute) ||
			    (Context.mCapabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG &&
			        Context.mQueue != EArdaRHIQueueType::Graphics))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "CUDA execution mode does not support this queue.");
			}
			return {};
		}

		FArdaRHIStatus EnsureBindings() const
		{
			std::call_once(mBindOnce,
			    [&]
			    {
				    mBindingStatus = ParameterType::GetCudaMetadata().GetStatus();
				    if (!mBindingStatus)
				    {
					    return;
				    }
				    try
				    {
					    BindKernelVariants(mRegistry);
					    mBindingStatus = mRegistry.Freeze(HasComputeAlternative());
				    }
				    catch (const std::exception& E)
				    {
					    mBindingStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, E.what());
				    }
				    catch (...)
				    {
					    mBindingStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					        "CUDA variant binding threw an exception.");
				    }
			    });
			return mBindingStatus;
		}

		FArdaRHIDeviceRef mDevice;
		mutable std::once_flag mBindOnce;
		mutable FArdaRegistry mRegistry;
		mutable FArdaRHIStatus mBindingStatus;
	};
}
