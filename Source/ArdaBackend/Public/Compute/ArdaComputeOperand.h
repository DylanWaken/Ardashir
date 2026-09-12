/** @file ArdaComputeOperand.h
 * Single-operation operands with user binding/selection and framework-owned dispatch.
 */
#pragma once
#include "ArdaCudaKernelVariants.h"
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
		virtual FArdaRHIStatus GetOperandSupport() const = 0;
		virtual const FArdaComputeParameterMetadata& GetParameterMetadata() const = 0;
	};

	/** Authors supply BindKernelVariants and SelectKernel. Dispatch cannot be overridden.
     * Binding is synchronized once per operand. Selection must have no GPU/host side effects.
     * Each dispatch retains its own frozen values/resources independently of the operand.
     */
	template <class ParameterType, class VariantPayload>
	class TArdaComputeOperand : public FArdaComputeOperand
	{
	public:
		using FParameters = ParameterType;
		using FKernelParameters = typename ParameterType::FCuda;
		using FRegistry = TArdaCudaKernelRegistry<ParameterType, VariantPayload>;
		using FVariants = typename FRegistry::FVariants;
		using FPlan = eastl::shared_ptr<const FArdaCudaDispatchPlan>;

		explicit TArdaComputeOperand(FArdaRHIDeviceRef Device)
		    : mDevice(eastl::move(Device))
		{
		}

		const FArdaComputeParameterMetadata& GetParameterMetadata() const final
		{
			return ParameterType::GetStaticMetadata();
		}

		/** Registers compiled symbols or external library factories and payloads without calling CUDA. */
		virtual void BindKernelVariants(FRegistry& Registry) const = 0;

		/** Chooses one compatible operation using host metadata; can fail or explicitly return NoWork. */
		virtual TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FParameters& Parameters,
		    const FArdaCudaSelectionContext& Context,
		    const FVariants& Candidates) const = 0;

		/** Initializes and returns a read-only registry with operand lifetime. */
		TArdaRHIResult<const FRegistry*> GetKernelVariants() const
		{
			auto S = EnsureBindings();
			return {S ? &mRegistry : nullptr, S};
		}

		/** Validates schema, immutable registry, device CUDA support and native target coverage.
         * Does not validate a particular resource allocation/view or launch a kernel.
         * @errors Returns the first binding error, InvalidArgument for no device, or Unsupported for missing mode/native coverage.
         * @threading Safe for concurrent inspection; binding initializes once without calling CUDA.
         */
		FArdaRHIStatus GetOperandSupport() const final
		{
			FArdaCudaSelectionContext Context;
			FVariants Candidates;
			return GetSelectionCandidates(Context, Candidates);
		}

		/** Freezes a validated launch plan. Parameters can be changed/destroyed after return. */
		virtual TArdaRHIResult<FPlan> PrepareDispatch(const FParameters& Parameters,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) const final
		{
			FArdaCudaSelectionContext Context{{}, Queue};
			FVariants Candidates;
			if (auto S = GetSelectionCandidates(Context, Candidates); !S)
			{
				return {{}, S};
			}
			if (Queue == EArdaRHIQueueType::Copy ||
			    (Context.mCapabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG &&
			        Queue != EArdaRHIQueueType::Graphics))
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				        "CUDA execution mode does not support this queue.")};
			}
			auto Plan = eastl::make_shared<FArdaCudaDispatchPlan>();
			Plan->mDevice = mDevice;
			Plan->mQueue = Queue;
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
		static FArdaRHIStatus RecordPlan(IArdaRHICommandList& Commands, const FPlan& Plan)
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
			return Plan->mSelection.mbNoWork ? FArdaRHIStatus{} : Commands.DispatchCuda(Plan->mDispatch);
		}

		/** Records one selected kernel or library operation into the caller-owned list; RDG uses this path. */
		virtual FArdaRHIStatus DispatchDeferred(IArdaRHICommandList& Commands,
		    const FParameters& Parameters) const final
		{
			auto Plan = PrepareDispatch(Parameters, Commands.GetQueueType());
			return Plan ? RecordPlan(Commands, Plan.mValue) : Plan.mStatus;
		}

		/** Submits the shared plan path and returns its queue completion identity. */
		virtual TArdaRHIResult<FArdaCudaSubmission> Dispatch(const FParameters& Parameters,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics) const final
		{
			auto Plan = PrepareDispatch(Parameters, Queue);
			if (!Plan)
			{
				return {{}, Plan.mStatus};
			}
			FArdaCudaSubmission Result{mDevice, Queue, 0, Plan.mValue->mSelection};
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
		FArdaRHIStatus GetSelectionCandidates(FArdaCudaSelectionContext& Context, FVariants& Candidates) const
		{
			if (auto S = EnsureBindings(); !S)
			{
				return S;
			}
			if (!mDevice)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA operand has no device.");
			}
			Context.mCapabilities = mDevice->GetCudaCapabilities();
			if (!Context.mCapabilities)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    Context.mCapabilities.mUnavailableReason.c_str());
			}
			Candidates = mRegistry.GetCompatibleVariants(Context.mCapabilities);
			if (Candidates.empty())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "No bound CUDA variant has compatible native code and execution requirements.");
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
					    mBindingStatus = mRegistry.Freeze();
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
		mutable FRegistry mRegistry;
		mutable FArdaRHIStatus mBindingStatus;
	};
}
