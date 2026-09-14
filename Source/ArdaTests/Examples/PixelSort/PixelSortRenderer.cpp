#include "PixelSortRenderer.h"
#include "ArdaExampleStatus.h"
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

	struct FArdaPixelSortRenderer::FArdaFrameGraph
	{
		explicit FArdaFrameGraph(FArdaRHIDeviceRef Device)
		    : mGraph(eastl::move(Device))
		{
		}

		FArdaDependencyGraph mGraph;
		FArdaRHITextureRef mColor;
		eastl::shared_ptr<FArdaPixelSortUploadFrameInput> mUploadInput;
		eastl::shared_ptr<FArdaPixelSortSortInput> mSortInput;
		eastl::shared_ptr<FArdaPixelSortReadback> mNoiseReadback;
		eastl::shared_ptr<FArdaPixelSortReadback> mSortedReadback;
		eastl::shared_ptr<FArdaPixelSortReadback> mFrameReadback;
		bool mbVerify = false;
		bool mbCapture = false;
	};

	FArdaPixelSortRenderer::FArdaPixelSortRenderer(FArdaRHIDeviceRef Device)
	    : mDevice(eastl::move(Device))
	{
	}

	FArdaPixelSortRenderer::~FArdaPixelSortRenderer() = default;

	void FArdaPixelSortRenderer::ReleaseFrameGraphs()
	{
		// Graph destruction retires its own tickets before releasing imported back buffers.
		mFrames.clear();
	}

	FArdaPixelSortRenderer::FArdaFrameGraph& FArdaPixelSortRenderer::FindOrCreateFrameGraph(
	    const FArdaRHITextureRef& Color,
	    uint32_t Width,
	    uint32_t Height,
	    bool bVerify,
	    bool bCapture)
	{

		// Reuse a compiled graph while its back buffer and diagnostic outputs are unchanged.
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

		// Assemble the upload, graphics noise, CUDA sort, and presentation dependency chain.
		auto Frame = std::make_unique<FArdaFrameGraph>(mDevice);
		Frame->mColor = Color;
		Frame->mbVerify = bVerify;
		Frame->mbCapture = bCapture;
		auto& Graph = Frame->mGraph;
		CheckArdaExampleStatus(Graph.BeginGraphEdit());
		const auto ColorTarget = TakeArdaExampleValue(Graph.ImportTexture("Swap-chain color", Color));
		FArdaPixelSortUploadFrameParameters UploadParameters;
		UploadParameters.mWidth = Width;
		UploadParameters.mHeight = Height;
		Frame->mUploadInput = UploadParameters.mInput;
		const auto Upload =
		    TakeArdaExampleValue(Graph.AttachOrFind<FArdaPixelSortUploadFrameNode>("Upload frame", UploadParameters));
		const auto Constants = Graph.FindOutput(Upload, "Constants");

		const auto Noise = TakeArdaExampleValue(
		    Graph.AttachOrFind<FArdaPixelSortNoiseNode>("Generate noise", {Constants, {}, Width, Height}));
		const auto NoiseTexture = Graph.FindOutput(Noise, "Output");

		FArdaPixelSortSortParameters SortParameters;
		SortParameters.mNoise = NoiseTexture;
		SortParameters.mWidth = Width;
		SortParameters.mHeight = Height;
		Frame->mSortInput = SortParameters.mInput;
		const auto Sort =
		    TakeArdaExampleValue(Graph.AttachOrFind<FArdaPixelSortSortNode>("Radix sort", SortParameters));
		const auto SortedTexture = Graph.FindOutput(Sort, "Output");
		TakeArdaExampleValue(Graph.AttachOrFind<FArdaPixelSortPresentNode>("Present pixels",
		    {Constants, NoiseTexture, SortedTexture, ColorTarget, Width, Height}));

		// Add CPU-visible outputs only for requested verification or image capture.
		if (bVerify)
		{
			Frame->mNoiseReadback = eastl::make_shared<FArdaPixelSortReadback>();
			Frame->mSortedReadback = eastl::make_shared<FArdaPixelSortReadback>();
			TakeArdaExampleValue(
			    Graph.AttachOrFind<FArdaPixelSortReadbackNode>("Read noise", {NoiseTexture, Frame->mNoiseReadback}));
			TakeArdaExampleValue(Graph.AttachOrFind<FArdaPixelSortReadbackNode>("Read sorted pixels",
			    {SortedTexture, Frame->mSortedReadback}));
		}
		if (bCapture)
		{
			Frame->mFrameReadback = eastl::make_shared<FArdaPixelSortReadback>();
			TakeArdaExampleValue(
			    Graph.AttachOrFind<FArdaPixelSortReadbackNode>("Capture frame", {ColorTarget, Frame->mFrameReadback}));
		}

		// Compile before replacing the cached graph so attachment failures leave it intact.
		CheckArdaExampleStatus(Graph.EndGraphEdit());
		if (Found == mFrames.end())
		{
			mFrames.push_back(std::move(Frame));
			return *mFrames.back();
		}
		*Found = std::move(Frame);
		return **Found;
	}

	void FArdaPixelSortRenderer::Render(IArdaSwapChain& SwapChain,
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

		// Sample frame-varying parameters without changing graph topology or pipeline identity.
		*Frame.mUploadInput = {Time, Original};
		*Frame.mSortInput = {Channel, Threshold};

		// Submit asynchronous graph work and present the acquired image.
		SwapChain.PrepareSubmit();
		const auto Ticket = TakeArdaExampleValue(Frame.mGraph.Submit());
		if (!SwapChain.Present())
		{
			throw std::runtime_error(SwapChain.GetError().c_str());
		}

		// Consume readbacks only after completion of the submitted frame.
		if (Verify || !Capture.empty())
		{
			// Only diagnostics require immediate CPU completion; ordinary frames keep the ticket in the graph.
			CheckArdaExampleStatus(Frame.mGraph.Wait(Ticket).mStatus);
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
