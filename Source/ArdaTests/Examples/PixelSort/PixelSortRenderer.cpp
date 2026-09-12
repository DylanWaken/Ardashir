#include "PixelSortRenderer.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace arda
{
	namespace
	{
		void Check(FArdaRHIStatus Status)
		{
			if (!Status)
			{
				throw std::runtime_error(Status.mMessage.c_str());
			}
		}

		template <class T>
		T Take(TArdaRHIResult<T> Result)
		{
			Check(Result.mStatus);
			return eastl::move(Result.mValue);
		}

		uint32_t Luma(uint32_t Pixel)
		{
			return (54 * (Pixel & 255) + 183 * ((Pixel >> 8) & 255) + 19 * ((Pixel >> 16) & 255)) >> 8;
		}

		// Validation-only oracle: use std::stable_sort on each bright run instead
		// of reproducing the CUDA radix algorithm. Comparing complete pixels checks
		// stable equal-key ordering, dark boundaries, alpha and channel preservation.
		void VerifySort(const std::vector<uint32_t>& Input,
		    const std::vector<uint32_t>& Output,
		    uint32_t Width,
		    uint32_t Height,
		    uint32_t Channel,
		    uint32_t Threshold)
		{
			const bool Vertical = Height > Width;

			// Match the sample's public selection/tile policy. Sorting is bounded
			// by both dark pixels and tile edges; a global row sort would differ.
			const uint32_t Tile = Vertical ? 1024 : 512, Length = Vertical ? Height : Width,
			               Lines = Vertical ? Width : Height;
			auto Expected = Input;
			for (uint32_t Line = 0; Line < Lines; ++Line)
			{
				for (uint32_t Begin = 0; Begin < Length; Begin += Tile)
				{
					const uint32_t Count = std::min(Tile, Length - Begin);
					const auto Index = [&](uint32_t I)
					{
						return Vertical ? (Begin + I) * Width + Line : Line * Width + Begin + I;
					};
					std::vector<uint32_t> Values(Count);
					for (uint32_t I = 0; I < Count; ++I)
					{
						Values[I] = Input[Index(I)];
					}
					for (uint32_t I = 0; I < Count;)
					{
						if (Luma(Values[I]) < Threshold)
						{
							++I;
							continue;
						}
						const auto First = I;
						while (I < Count && Luma(Values[I]) >= Threshold)
						{
							++I;
						}
						std::stable_sort(Values.begin() + First,
						    Values.begin() + I,
						    [Channel](uint32_t A, uint32_t B)
						    {
							    return ((A >> (Channel * 8)) & 255) < ((B >> (Channel * 8)) & 255);
						    });
					}
					for (uint32_t I = 0; I < Count; ++I)
					{
						Expected[Index(I)] = Values[I];
					}
				}
			}
			const auto Mismatch = std::mismatch(Expected.begin(), Expected.end(), Output.begin());
			if (Mismatch.first != Expected.end())
			{
				throw std::runtime_error("CUDA radix output differs from the CPU stable-sort reference at pixel " +
				    std::to_string(Mismatch.first - Expected.begin()));
			}
			if (std::all_of(Input.begin(),
			        Input.end(),
			        [&](uint32_t V)
			        {
				        return V == Input.front();
			        }))
			{
				throw std::runtime_error("Noise compute shader produced a uniform image.");
			}
			std::printf("Verified %ux%u, %s, channel %u, %u threads, tile %u: exact RGBA match.\n",
			    Width,
			    Height,
			    Vertical ? "columns" : "rows",
			    Channel,
			    Vertical ? 256 : 128,
			    Tile);
		}

		// Capture encodes the rendered back buffer, not the integer CUDA surface.
		// WIC accepts BGRA bytes here; swizzle an RGBA swap chain explicitly. This
		// is an export conversion and is unrelated to CUDA resource-handle mapping.
		void SavePng(const std::filesystem::path& Path,
		    uint32_t Width,
		    uint32_t Height,
		    std::vector<uint32_t> Pixels,
		    EArdaRHIFormat Format)
		{
			using Microsoft::WRL::ComPtr;
			const auto Checked = [](HRESULT Result)
			{
				if (FAILED(Result))
				{
					throw std::runtime_error("PNG encoding failed.");
				}
			};
			if (Format == EArdaRHIFormat::RGBA8UNorm || Format == EArdaRHIFormat::SRGBA8UNorm)
			{
				for (auto& P : Pixels)
				{
					P = (P & 0xff00ff00u) | ((P & 255) << 16) | ((P >> 16) & 255);
				}
			}
			else if (Format != EArdaRHIFormat::BGRA8UNorm && Format != EArdaRHIFormat::SBGRA8UNorm)
			{
				throw std::runtime_error("Capture requires an RGBA8/BGRA8 swap chain.");
			}
			ComPtr<IWICImagingFactory> Factory;
			Checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&Factory)));
			ComPtr<IWICStream> Stream;
			Checked(Factory->CreateStream(&Stream));
			Checked(Stream->InitializeFromFilename(Path.c_str(), GENERIC_WRITE));
			ComPtr<IWICBitmapEncoder> Encoder;
			Checked(Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &Encoder));
			Checked(Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache));
			ComPtr<IWICBitmapFrameEncode> Frame;
			Checked(Encoder->CreateNewFrame(&Frame, nullptr));
			Checked(Frame->Initialize(nullptr));
			Checked(Frame->SetSize(Width, Height));
			WICPixelFormatGUID PixelFormat = GUID_WICPixelFormat32bppBGRA;
			Checked(Frame->SetPixelFormat(&PixelFormat));
			if (PixelFormat != GUID_WICPixelFormat32bppBGRA)
			{
				throw std::runtime_error("PNG encoder changed its pixel format.");
			}
			Checked(Frame->WritePixels(Height,
			    Width * 4,
			    uint32_t(Pixels.size() * 4),
			    reinterpret_cast<BYTE*>(Pixels.data())));
			Checked(Frame->Commit());
			Checked(Encoder->Commit());
		}
	}

	struct FPixelSortRenderer::FArdaFrameGraph
	{
		explicit FArdaFrameGraph(FArdaRHIDeviceRef Device)
		    : mGraph(eastl::move(Device))
		{
		}

		FArdaDependencyGraph mGraph;
		FArdaRHITextureRef mColor;
		eastl::shared_ptr<FArdaPixelSortFrameInput> mInput;
		eastl::shared_ptr<FArdaPixelSortReadback> mNoiseReadback;
		eastl::shared_ptr<FArdaPixelSortReadback> mSortedReadback;
		eastl::shared_ptr<FArdaPixelSortReadback> mFrameReadback;
		bool mbVerify = false;
		bool mbCapture = false;
	};

	FPixelSortRenderer::FPixelSortRenderer(FArdaRHIDeviceRef Device)
	    : mDevice(eastl::move(Device))
	{
	}

	FPixelSortRenderer::~FPixelSortRenderer() = default;

	void FPixelSortRenderer::ReleaseFrameGraphs()
	{
		// Graph destruction retires its own tickets before releasing imported back buffers.
		mFrames.clear();
	}

	FPixelSortRenderer::FArdaFrameGraph& FPixelSortRenderer::FindOrCreateFrameGraph(const FArdaRHITextureRef& Color,
	    uint32_t Width,
	    uint32_t Height,
	    bool bVerify,
	    bool bCapture)
	{
		auto Found = std::find_if(mFrames.begin(),
		    mFrames.end(),
		    [&](const auto& Frame)
		    {
			    return Frame->mColor == Color;
		    });
		if (Found != mFrames.end() && (*Found)->mbVerify == bVerify && (*Found)->mbCapture == bCapture)
		{
			return **Found;
		}
		auto Frame = std::make_unique<FArdaFrameGraph>(mDevice);
		Frame->mColor = Color;
		Frame->mbVerify = bVerify;
		Frame->mbCapture = bCapture;
		auto& Graph = Frame->mGraph;
		Check(Graph.BeginGraphEdit());
		FArdaPixelSortNodeParameters P;
		P.mWidth = Width;
		P.mHeight = Height;
		Frame->mInput = P.mInput;
		P.mColor = Take(Graph.ImportTexture("Swap-chain color", Color));
		FArdaRHIBufferDesc Constants;
		Constants.mByteSize = 256;
		Constants.mUsage = EArdaRHIBufferUsage::Constant;
		P.mConstants = Take(Graph.CreateBuffer("Frame constants", Constants));
		FArdaRHITextureDesc Texture;
		Texture.mWidth = Width;
		Texture.mHeight = Height;
		Texture.mFormat = EArdaRHIFormat::RGBA8UInt;
		Texture.mbCudaInterop = true;
		Texture.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		P.mNoise = Take(Graph.CreateTexture("Animated noise", Texture));
		P.mSorted = Take(Graph.CreateTexture("Sorted pixels", Texture));

		// Attach consumers first: resource dependencies recover upload -> noise -> CUDA -> presentation.
		Take(Graph.AttachOrFind<FArdaPixelSortPresentNode>("Present pixels", P));
		Take(Graph.AttachOrFind<FArdaPixelSortSortNode>("Radix sort", P));
		Take(Graph.AttachOrFind<FArdaPixelSortNoiseNode>("Generate noise", P));
		Take(Graph.AttachOrFind<FArdaPixelSortUploadFrameNode>("Upload frame", P));
		if (bVerify)
		{
			Frame->mNoiseReadback = eastl::make_shared<FArdaPixelSortReadback>();
			Frame->mSortedReadback = eastl::make_shared<FArdaPixelSortReadback>();
			Take(Graph.AttachOrFind<FArdaPixelSortReadbackNode>("Read noise", {P.mNoise, Frame->mNoiseReadback}));
			Take(Graph.AttachOrFind<FArdaPixelSortReadbackNode>("Read sorted pixels",
			    {P.mSorted, Frame->mSortedReadback}));
		}
		if (bCapture)
		{
			Frame->mFrameReadback = eastl::make_shared<FArdaPixelSortReadback>();
			Take(Graph.AttachOrFind<FArdaPixelSortReadbackNode>("Capture frame", {P.mColor, Frame->mFrameReadback}));
		}
		Check(Graph.EndGraphEdit());
		if (Found == mFrames.end())
		{
			mFrames.push_back(std::move(Frame));
			return *mFrames.back();
		}
		*Found = std::move(Frame);
		return **Found;
	}

	void FPixelSortRenderer::Render(IArdaSwapChain& SwapChain,
	    float Time,
	    uint32_t Channel,
	    uint32_t Threshold,
	    bool Original,
	    bool Verify,
	    const std::filesystem::path& Capture)
	{
		FArdaRHIFramebufferRef Framebuffer;
		if (!SwapChain.AcquireFrame(Framebuffer))
		{
			throw std::runtime_error(SwapChain.GetError().c_str());
		}
		const auto Color = Framebuffer->GetDesc().mColorAttachments.front().mTexture;
		const auto Width = SwapChain.GetWidth();
		const auto Height = SwapChain.GetHeight();
		auto& Frame = FindOrCreateFrameGraph(Color, Width, Height, Verify, !Capture.empty());
		*Frame.mInput = {Time, Channel, Threshold, Original};
		SwapChain.PrepareSubmit();
		const auto Ticket = Take(Frame.mGraph.Submit());
		if (!SwapChain.Present())
		{
			throw std::runtime_error(SwapChain.GetError().c_str());
		}
		if (Verify || !Capture.empty())
		{
			// Only diagnostics require immediate CPU completion; ordinary frames keep the ticket in the graph.
			Check(Frame.mGraph.Wait(Ticket).mStatus);
			if (Verify)
			{
				VerifySort(FArdaPixelSortReadbackNode::ReadPixels(mDevice, *Frame.mNoiseReadback),
				    FArdaPixelSortReadbackNode::ReadPixels(mDevice, *Frame.mSortedReadback),
				    Width,
				    Height,
				    Channel,
				    Threshold);
			}
			if (!Capture.empty())
			{
				SavePng(Capture,
				    Width,
				    Height,
				    FArdaPixelSortReadbackNode::ReadPixels(mDevice, *Frame.mFrameReadback),
				    Color->GetDesc().mFormat);
			}
		}
		mDevice->RunGarbageCollection();
	}
}
