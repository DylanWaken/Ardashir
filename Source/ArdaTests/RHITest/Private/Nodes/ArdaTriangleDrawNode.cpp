#include "ArdaRHITestPch.h"
#include "Nodes/ArdaTriangleDrawNode.h"
#include "ArdaDependencyGraphNodes.h"
#include <cstring>
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include "ArdaExamplePaths.h"

namespace arda
{
	struct FArdaTriangleDrawNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
		eastl::string mError;

		static bool LoadBinary(const std::filesystem::path& path, eastl::vector<uint8_t>& binary, eastl::string& error)
		{
			const std::string pathString = path.string();
			const eastl::string displayPath(pathString.data(), pathString.size());
			std::ifstream stream(path, std::ios::binary | std::ios::ate);
			if (!stream)
			{
				error = "Unable to open shader: " + displayPath;
				return false;
			}
			const auto size = stream.tellg();
			if (size <= 0)
			{
				error = "Shader is empty: " + displayPath;
				return false;
			}
			binary.resize(static_cast<size_t>(size));
			stream.seekg(0);
			stream.read(reinterpret_cast<char*>(binary.data()), size);
			if (!stream)
			{
				error = "Unable to read shader: " + displayPath;
				binary.clear();
				return false;
			}
			return true;
		}

		bool InitializeShaders()
		{
			const auto Device = mDevice;
			const auto ShaderDirectory = GetArdaExampleDirectory();
			const std::filesystem::path shaderCache = ShaderDirectory / ".arda-cache" / "shaders";
			const std::string shaderSource =
			    (std::filesystem::path(ARDA_RHI_TEST_SHADER_SOURCE_DIR) / "ArdaTriangle.hlsl").string();
			arda::FArdaShaderTypeRegistration vertexRegistration("RHITestTriangleVertex",
			    shaderSource.c_str(),
			    "TriangleVS",
			    "VSMain",
			    arda::EArdaRHIShaderStage::Vertex,
			    nullptr);
			arda::FArdaShaderTypeRegistration pixelRegistration("RHITestTrianglePixel",
			    shaderSource.c_str(),
			    "TrianglePS",
			    "PSMain",
			    arda::EArdaRHIShaderStage::Pixel,
			    nullptr);
			const arda::FArdaShaderCompilerConfiguration previousCompiler = arda::GetShaderCompilerConfiguration();
			arda::FArdaShaderCompilerConfiguration runtimeCompiler = previousCompiler;
			runtimeCompiler.mbCompileMissingArtifacts = true;
			runtimeCompiler.mbCompileOutdatedArtifacts = true;
			arda::ConfigureShaderCompiler(runtimeCompiler);
			const arda::FArdaShaderCompileResult compileResult = arda::EnsureRegisteredShaderArtifacts(shaderCache,
			    arda::GetBackendConfiguration().mBackendName.c_str());
			arda::ConfigureShaderCompiler(previousCompiler);
			if (!compileResult)
			{
				mError = compileResult.mDiagnostics.empty() ? "Ardashir failed to compile the triangle shaders."
				                                            : compileResult.mDiagnostics.front().mMessage;
				return false;
			}

			const char* ArtifactExtension =
			    arda::GetShaderArtifactExtension(arda::GetBackendConfiguration().mBackendName.c_str());
			eastl::vector<uint8_t> vertexBinary;
			eastl::vector<uint8_t> pixelBinary;
			if (!LoadBinary(shaderCache / (std::string("TriangleVS") + ArtifactExtension), vertexBinary, mError) ||
			    !LoadBinary(shaderCache / (std::string("TrianglePS") + ArtifactExtension), pixelBinary, mError))
			{
				return false;
			}

			arda::FArdaRHIShaderDesc shaderDesc;
			shaderDesc.mStage = arda::EArdaRHIShaderStage::Vertex;
			shaderDesc.mBytecode = vertexBinary.data();
			shaderDesc.mBytecodeSize = vertexBinary.size();
			shaderDesc.mEntryPoint = "VSMain";
			shaderDesc.mDebugName = "Triangle vertex shader";
			auto vertexShader = Device->CreateShader(shaderDesc);
			shaderDesc.mStage = arda::EArdaRHIShaderStage::Pixel;
			shaderDesc.mBytecode = pixelBinary.data();
			shaderDesc.mBytecodeSize = pixelBinary.size();
			shaderDesc.mEntryPoint = "PSMain";
			shaderDesc.mDebugName = "Triangle pixel shader";
			auto pixelShader = Device->CreateShader(shaderDesc);
			if (!vertexShader || !pixelShader)
			{
				mError = "RHI failed to create the triangle shaders.";
				return false;
			}
			const auto VertexShader = eastl::move(vertexShader.mValue);
			const auto PixelShader = eastl::move(pixelShader.mValue);

			eastl::vector<arda::FArdaRHIVertexAttributeDesc> attributes(2);
			attributes[0].mSemanticName = "POSITION";
			attributes[0].mFormat = arda::EArdaRHIFormat::RG32Float;
			attributes[0].mOffset = offsetof(FArdaTriangleVertex, mPosition);
			attributes[0].mElementStride = sizeof(FArdaTriangleVertex);
			attributes[1].mSemanticName = "COLOR";
			attributes[1].mFormat = arda::EArdaRHIFormat::RGB32Float;
			attributes[1].mOffset = offsetof(FArdaTriangleVertex, mColor);
			attributes[1].mElementStride = sizeof(FArdaTriangleVertex);
			auto inputLayout = Device->CreateInputLayout(attributes);
			if (!inputLayout)
			{
				mError = inputLayout.mStatus.mMessage;
				return false;
			}
			const auto InputLayout = eastl::move(inputLayout.mValue);

			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Graphics;
			auto& pipelineDesc = Configuration->mGraphics.mDesc;
			pipelineDesc.mInputLayout = InputLayout;
			pipelineDesc.mVertexShader = VertexShader;
			pipelineDesc.mPixelShader = PixelShader;
			pipelineDesc.mDepthStencilState.mbDepthTest = false;
			pipelineDesc.mDepthStencilState.mbDepthWrite = false;
			pipelineDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
			pipelineDesc.mDebugName = "Triangle pipeline";
			mConfiguration = eastl::move(Configuration);

			mError.clear();
			return true;
		}

		FArdaRHIStatus Initialize()
		{
			try
			{
				return InitializeShaders() ? FArdaRHIStatus{}
				                           : FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, mError.c_str());
			}
			catch (const std::exception& Error)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Error.what());
			}
		}
	};

	FArdaDependencyNodeMetadata FArdaTriangleDrawNode::GetMetadata()
	{
		return {"example.triangle.draw", 1};
	}

	eastl::string FArdaTriangleDrawNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string K;
		auto Add = [&K](auto Value)
		{
			K.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
		};
		for (auto H : {P.mColor, P.mVertices, P.mIndices})
		{
			Add(H.mGraph);
			Add(H.mIndex);
			Add(H.mGeneration);
		}
		Add(P.mWidth);
		Add(P.mHeight);
		return K;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTriangleDrawNode::FState>> FArdaTriangleDrawNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}
		auto State = eastl::make_shared<FState>();
		State->mDevice = eastl::move(Device);
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTriangleDrawNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc R;
		R.mAccesses = {{P.mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::VertexBuffer},
		    {P.mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::IndexBuffer},
		    {P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		R.mColorTargets = {P.mColor};
		R.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};
		return R;
	}

	FArdaRHIStatus FArdaTriangleDrawNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto Status = C.GetCommands().ClearTexture(*C.GetTexture(P.mColor), {}, {0.025f, 0.035f, 0.06f, 1.f});
		if (!Status)
		{
			return Status;
		}
		FArdaRHIGraphicsState State;
		State.mPipeline = C.GetPipeline()->mGraphics;
		State.mFramebuffer = C.GetFramebuffer();
		State.mVertexBuffers.push_back({C.GetBuffer(P.mVertices), 0, 0});
		State.mIndexBuffer = C.GetBuffer(P.mIndices);
		State.mIndexFormat = EArdaRHIFormat::R16UInt;
		State.mViewports.push_back({0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f});
		State.mScissors.push_back({0, int32_t(P.mWidth), 0, int32_t(P.mHeight)});
		Status = C.GetCommands().SetGraphicsState(State);
		if (!Status)
		{
			return Status;
		}
		C.GetCommands().DrawIndexed({3});
		return {};
	}
}
