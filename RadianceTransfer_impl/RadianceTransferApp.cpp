//***************************************************************************************
// NormalMapApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/Camera.h"
#include "FrameResource.h"
#include "DXRHelper.h"
#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include "nv_helpers_dx12/TopLevelASGenerator.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"
#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"
#include "Model.h"
#include "ShadowMap.h"

#include <mutex>
#include <dxcapi.h>
#include <vector>
#include <iostream>
#include <exception>
#include <random>
#include <limits>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 1;

struct SHCoeff
{
	DirectX::XMFLOAT3 SHCoeff_l0_m0;
	DirectX::XMFLOAT3 SHCoeff_l1_m_1;
	DirectX::XMFLOAT3 SHCoeff_l1_m0;
	DirectX::XMFLOAT3 SHCoeff_l1_m1;
	DirectX::XMFLOAT3 SHCoeff_l2_m_2;
	DirectX::XMFLOAT3 SHCoeff_l2_m_1;
	DirectX::XMFLOAT3 SHCoeff_l2_m0;
	DirectX::XMFLOAT3 SHCoeff_l2_m1;
	DirectX::XMFLOAT3 SHCoeff_l2_m2;
};

struct RandomState
{
	using uint = typename unsigned int;
	uint z1;
	uint z2;
	uint z3;
	uint z4;
	float u;
	float v;
};

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
	RenderItem(const RenderItem& rhs) = delete;
	void strafe(float d) {
		// Position += d*Right
		XMVECTOR s = XMVectorReplicate(d);
		XMVECTOR r = XMLoadFloat3(&Right);
		XMVECTOR p = XMLoadFloat3(&Position);
		XMStoreFloat3(&Position, XMVectorMultiplyAdd(s, r, p));

		NumFramesDirty = gNumFrameResources;
	}

	void walk(float d) {
		// Position += d*Front
		XMVECTOR s = XMVectorReplicate(d);
		XMVECTOR r = XMLoadFloat3(&Front);
		XMVECTOR p = XMLoadFloat3(&Position);
		XMStoreFloat3(&Position, XMVectorMultiplyAdd(s, r, p));

		NumFramesDirty = gNumFrameResources;
	}

	void fluctuate(float d) {
		// Position += d*Up
		XMVECTOR s = XMVectorReplicate(d);
		XMVECTOR l = XMLoadFloat3(&Up);
		XMVECTOR p = XMLoadFloat3(&Position);
		XMStoreFloat3(&Position, XMVectorMultiplyAdd(s, l, p));

		NumFramesDirty = gNumFrameResources;
	}

	std::string GeoName;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	constexpr static DirectX::XMFLOAT3 Right = { 1.0f, 0.0f, 0.0f };
	constexpr static DirectX::XMFLOAT3 Front = { 0.0f, 0.0f, 1.0f };
	constexpr static DirectX::XMFLOAT3 Up = { 0.0f, 1.0f, 0.0f };

	XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
	XMMATRIX WorldMat = XMMatrixIdentity();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	UINT vertexOffset = 0; // Vertex offset for visibility buffer.(model -> box -> grid)

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT VertexCount = 0;
	UINT StartIndexLocation = 0;
	UINT BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Sky,
	DiffuseRT,
	DiffuseRTTest,
	BVH,
	Filter,
	Count
};

enum class Space : int
{
	WorldSpace = 0,
	ScreenSpace,
	TextureSpace
};

class NormalMapApp : public D3DApp
{
public:
	NormalMapApp(HINSTANCE hInstance);
	NormalMapApp(const NormalMapApp& rhs) = delete;
	NormalMapApp& operator=(const NormalMapApp& rhs) = delete;
	~NormalMapApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();

	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildSHCoeffsBuffer();
	void BuildVisibilityTermBuffer();
	void BuildRandomStateBuffer(); // random number state for generating sampleVec
	void BuildGBuffer();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItemsIndexedInstanced(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawRenderItemsInstanced(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawSceneToDepthMap();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
	ComPtr<ID3D12Resource> uploader; // upload heap used to create random numbers buffer
	ComPtr<ID3D12Resource> copySource; // copy initial random state to random numbers buffer

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	UINT mCbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> mClearDescriptorHeap = nullptr; // descriptor heap for clearing UAV resources.

	ComPtr<ID3D12Resource> mEnvCoeffs = nullptr;

	// Per-vertex
	ComPtr<ID3D12Resource> mTemporalObjCoeffs = nullptr;
	ComPtr<ID3D12Resource> mThisFrameObjCoeffs = nullptr;

	// Per-pixel
	vector<ComPtr<ID3D12Resource>> mIntermediateScreenSpaceSHCoeffsBuffer;
	vector<ComPtr<ID3D12Resource>> mThisFrameScreenSpaceSHCoeffsBuffer;
	vector<ComPtr<ID3D12Resource>> mLastFrameScreenSpaceSHCoeffsBuffer;
	vector<ComPtr<ID3D12Resource>> mFilteredHorzSHCoeffsBuffer;
	vector<ComPtr<ID3D12Resource>> mFilteredVertSHCoeffsBuffer;

	vector<ComPtr<ID3D12Resource>> mGBuffer;

	ComPtr<ID3D12Resource> mVisibilityBuffer = nullptr;
	ComPtr<ID3D12Resource> mTextureSpaceVisibilityBuffer = nullptr;

	ComPtr<ID3D12Resource> mRandomStateBuffer = nullptr; // State buffer for generating random numbers

	std::unique_ptr<ShadowMap> mDepthMap = nullptr; // deptp map for screen space RT 

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];
	RenderItem* mGridRitem = nullptr;
	RenderItem* mBoxRitem = nullptr;

	// Projecting light transport in which space?
	Space mProjLTSpace = Space::ScreenSpace;

	UINT mSkyTexHeapIndex = 0;
	UINT mAccelerationStructureHeapIndex = 0;
	UINT mScreenSpaceIntermediateSHCoeffsHeapIndex = 0;
	UINT mScreenSpaceThisFrameSHCoeffsHeapIndex = 0;
	UINT mScreenSpaceLastFrameSHCoeffsHeapIndex = 0;
	UINT mFilteredHorzSHCoeffsHeapIndex = 0;
	UINT mFilteredVertSHCoeffsHeapIndex = 0;
	UINT mGBufferHeapIndex = 0;
	UINT mDepthMapHeapIndex = 0;
	UINT mTextureSpaceVisibility4SRVHeapIndex = 0;
	UINT mTextureSpaceVisibility4UAVHeapIndex = 0;

	// heap index for clearing UAV
	UINT mIntermediateClearHeapIndex = 0;
	UINT mThisFrameClearHeapIndex = 0;
	UINT mFilteredHorzClearHeapIndex = 0;
	UINT mFilteredVertClearHeapIndex = 0;
	UINT mGBufferClearHeapIndex = 0;

	PassConstants mMainPassCB;

	Camera mCamera;

	POINT mLastMousePos;

	// #DXR
	void CheckRaytracingSupport();

	struct AccelerationStructureBuffers {
		ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder
		ComPtr<ID3D12Resource> pResult;       // Where the AS is
		ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances
	};

	nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
	AccelerationStructureBuffers m_topLevelASBuffers;
	std::unordered_map<std::string, ComPtr<ID3D12Resource>> m_bottomLevelASBuffers;
	std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

	/// Create the acceleration structure of an instance
	///
	/// \param     vVertexBuffers : pair of buffer and vertex count
	/// \return    AccelerationStructureBuffers for TLAS
	AccelerationStructureBuffers CreateBottomLevelAS(
		std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
		std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers =
		{});

	/// Create the main acceleration structure that holds
	/// all instances of the scene
	/// \param     instances : pair of BLAS and transform
	// #DXR Extra - Refitting
	/// \param     updateOnly: if true, perform a refit instead of a full build
	void CreateTopLevelAS(
		const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>
		& instances,
		bool updateOnly = false);

	void BuildAccelerationStructure();

	ComPtr<ID3D12RootSignature> CreateRayGenSignature();
	ComPtr<ID3D12RootSignature> CreateMissSignature();
	ComPtr<ID3D12RootSignature> CreateHitSignature();

	void CreateRaytracingPipeline();

	ComPtr<IDxcBlob> m_rayGenLibrary;
	ComPtr<IDxcBlob> m_textureSpaceRayGenLibrary;
	ComPtr<IDxcBlob> m_hitLibrary;
	ComPtr<IDxcBlob> m_missLibrary;

	ComPtr<ID3D12RootSignature> m_rayGenSignature;
	ComPtr<ID3D12RootSignature> m_hitSignature;
	ComPtr<ID3D12RootSignature> m_missSignature;

	// Ray tracing pipeline state
	ComPtr<ID3D12StateObject> m_rtStateObject;
	// Ray tracing pipeline state properties, retaining the shader identifiers
	// to use in the Shader Binding Table
	ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

	void CreateShaderBindingTable(int diffuseRTIndex);
	nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
	ComPtr<ID3D12Resource> m_sbtStorage;

	void CalcVisibilityTerm();
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		NormalMapApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

NormalMapApp::NormalMapApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mMainWndCaption = L"Dynamic Radiance Transfer";
}

NormalMapApp::~NormalMapApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool NormalMapApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	CheckRaytracingSupport();

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(7.5f, 4.5f, 1.0f);
	mCamera.Pitch(XMConvertToRadians(25.0f));
	mCamera.RotateY(XMConvertToRadians(-75.0f));

	mDepthMap = std::make_unique<ShadowMap>(md3dDevice.Get(), mClientWidth, mClientHeight);

	LoadTextures();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRandomStateBuffer();
	BuildSHCoeffsBuffer();
	BuildVisibilityTermBuffer();
	BuildGBuffer();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildAccelerationStructure();
	BuildDescriptorHeaps();
	BuildPSOs();
	CreateRaytracingPipeline();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	copySource->Release();
	uploader->Release();

	return true;
}

void NormalMapApp::OnResize()
{
	D3DApp::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void NormalMapApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = 0;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
	UpdateObjectCBs(gt);

	// Update object's world matrix for refitting the BVH.
	for (int i = 0; i < m_instances.size(); ++i)
		m_instances[i].second = mRitemLayer[(int)RenderLayer::BVH][i]->WorldMat;
}

void NormalMapApp::Draw(const GameTimer& gt)
{

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
	mCommandList->SetGraphicsRootUnorderedAccessView(5, mEnvCoeffs->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootUnorderedAccessView(6, mTemporalObjCoeffs->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootUnorderedAccessView(7, mVisibilityBuffer->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootUnorderedAccessView(8, mRandomStateBuffer->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootUnorderedAccessView(9, mThisFrameObjCoeffs->GetGPUVirtualAddress());

	// Draw depth map.
	DrawSceneToDepthMap();

	// Bind the sky cube map.  For our demos, we just use one "world" cube map representing the environment
	// from far away, so all objects will use the same cube map and we only need to set it once per-frame.  
	// If we wanted to use "local" cube maps, we would have to change them per-object, or dynamically
	// index into an array of cube maps.

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(3, skyTexDescriptor);

	static std::once_flag flag;
	// precompute environment light
	std::call_once(flag,
		[&]() {
			mCommandList->SetPipelineState(mPSOs["proj_env"].Get());
			mCommandList->IASetVertexBuffers(0, 0, nullptr);
			mCommandList->IASetIndexBuffer(nullptr);
			mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			mCommandList->DrawInstanced(1, 1, 0, 0);
			mCommandList->SetPipelineState(mPSOs["opaque"].Get());
		});

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
	// set as a root descriptor.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Bind all the textures used in this scene.  Observe
	// that we only have to specify the first descriptor in the table.  
	// The root signature knows how many descriptors are expected in the table.
	mCommandList->SetGraphicsRootDescriptorTable(4, mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	CD3DX12_GPU_DESCRIPTOR_HANDLE screenSHCoeffsDescriptor(mDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	screenSHCoeffsDescriptor.Offset(mScreenSpaceIntermediateSHCoeffsHeapIndex, mCbvSrvDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(10, screenSHCoeffsDescriptor);

	mCommandList->SetGraphicsRootDescriptorTable(11, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mScreenSpaceThisFrameSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize));

	mCommandList->SetGraphicsRootDescriptorTable(12, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mScreenSpaceLastFrameSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize));

	mCommandList->SetGraphicsRootDescriptorTable(13, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mFilteredHorzSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize));

	mCommandList->SetGraphicsRootDescriptorTable(14, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mGBufferHeapIndex, mCbvSrvUavDescriptorSize));

	mCommandList->SetGraphicsRootDescriptorTable(15, CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mFilteredVertSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize));

	if (mProjLTSpace == Space::WorldSpace)
	{
		// Sample visibility.
		CalcVisibilityTerm();

		mCommandList->SetPipelineState(mPSOs["projLT"].Get());
		DrawRenderItemsInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::DiffuseRTTest]);

		mCommandList->SetPipelineState(mPSOs["reconstruct"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::DiffuseRTTest]);

		mCommandList->SetPipelineState(mPSOs["filter_horz_world"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		mCommandList->SetPipelineState(mPSOs["filter_vert_world"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		// zero out buffer
		static constexpr FLOAT clearValues[4] = { 0, 0, 0, 0 };
		mCommandList->ClearUnorderedAccessViewFloat(
			CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mScreenSpaceThisFrameSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mThisFrameClearHeapIndex, mCbvSrvUavDescriptorSize),
			mThisFrameScreenSpaceSHCoeffsBuffer[0].Get(), clearValues, 0, nullptr);

		mCommandList->ClearUnorderedAccessViewFloat(
			CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mFilteredHorzSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mFilteredHorzClearHeapIndex, mCbvSrvUavDescriptorSize),
			mFilteredHorzSHCoeffsBuffer[0].Get(), clearValues, 0, nullptr);

		for (int i = 0; i < 2; ++i)
		{
			mCommandList->ClearUnorderedAccessViewFloat(
				CD3DX12_GPU_DESCRIPTOR_HANDLE(
					mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mGBufferHeapIndex + i, mCbvSrvUavDescriptorSize),
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mGBufferClearHeapIndex + i, mCbvSrvUavDescriptorSize),
				mGBuffer[i].Get(), clearValues, 0, nullptr);
		}
	}

	else if (mProjLTSpace == Space::TextureSpace)
	{
		// Sample visibility.
		CalcVisibilityTerm();

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mTextureSpaceVisibilityBuffer.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

		mCommandList->SetPipelineState(mPSOs["projLTTextureSpace"].Get());
		DrawRenderItemsInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::DiffuseRT]);

		mCommandList->SetPipelineState(mPSOs["reconstruct"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::DiffuseRT]);

		mCommandList->SetPipelineState(mPSOs["filter_horz_world"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		mCommandList->SetPipelineState(mPSOs["filter_vert_world"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		// zero out buffer
		static constexpr FLOAT clearValues[4] = { 0, 0, 0, 0 };
		mCommandList->ClearUnorderedAccessViewFloat(
			CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mScreenSpaceThisFrameSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mThisFrameClearHeapIndex, mCbvSrvUavDescriptorSize),
			mThisFrameScreenSpaceSHCoeffsBuffer[0].Get(), clearValues, 0, nullptr);

		mCommandList->ClearUnorderedAccessViewFloat(
			CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mFilteredHorzSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mFilteredHorzClearHeapIndex, mCbvSrvUavDescriptorSize),
			mFilteredHorzSHCoeffsBuffer[0].Get(), clearValues, 0, nullptr);

		for (int i = 0; i < 2; ++i)
		{
			mCommandList->ClearUnorderedAccessViewFloat(
				CD3DX12_GPU_DESCRIPTOR_HANDLE(
					mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mGBufferHeapIndex + i, mCbvSrvUavDescriptorSize),
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mGBufferClearHeapIndex + i, mCbvSrvUavDescriptorSize),
				mGBuffer[i].Get(), clearValues, 0, nullptr);
		}

		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mTextureSpaceVisibilityBuffer.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

		mCommandList->SetPipelineState(mPSOs["opaque"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);
	}

	else if (mProjLTSpace == Space::ScreenSpace)
	{
		mCommandList->SetPipelineState(mPSOs["writeGBuffer"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::DiffuseRTTest]);

		// Sample visibility.
		CalcVisibilityTerm();

		mCommandList->SetPipelineState(mPSOs["screenSpaceProjLT"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		//mCommandList->SetPipelineState(mPSOs["outlier_removal"].Get());
		//DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		mCommandList->SetPipelineState(mPSOs["filter_horz"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		mCommandList->SetPipelineState(mPSOs["filter_vert"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		mCommandList->SetPipelineState(mPSOs["temporal_filter"].Get());
		DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Filter]);

		static constexpr FLOAT clearValues[4] = { 0, 0, 0, 0 };

		// Copy color
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mIntermediateScreenSpaceSHCoeffsBuffer[0].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE));
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mLastFrameScreenSpaceSHCoeffsBuffer[0].Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST));
		mCommandList->CopyResource(mLastFrameScreenSpaceSHCoeffsBuffer[0].Get(), mIntermediateScreenSpaceSHCoeffsBuffer[0].Get());
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mLastFrameScreenSpaceSHCoeffsBuffer[0].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mIntermediateScreenSpaceSHCoeffsBuffer[0].Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

		// zero out buffer
		for (int i = 0; i < 2; ++i) 
		{
			mCommandList->ClearUnorderedAccessViewFloat(
				CD3DX12_GPU_DESCRIPTOR_HANDLE(
					mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mScreenSpaceIntermediateSHCoeffsHeapIndex+i, mCbvSrvUavDescriptorSize),
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mIntermediateClearHeapIndex+i, mCbvSrvUavDescriptorSize),
				mIntermediateScreenSpaceSHCoeffsBuffer[i].Get(), clearValues, 0, nullptr);

			mCommandList->ClearUnorderedAccessViewFloat(
				CD3DX12_GPU_DESCRIPTOR_HANDLE(
					mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mScreenSpaceThisFrameSHCoeffsHeapIndex+i, mCbvSrvUavDescriptorSize),
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mThisFrameClearHeapIndex+i, mCbvSrvUavDescriptorSize),
				mThisFrameScreenSpaceSHCoeffsBuffer[i].Get(), clearValues, 0, nullptr);
		}

		mCommandList->ClearUnorderedAccessViewFloat(
			CD3DX12_GPU_DESCRIPTOR_HANDLE(
				mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mFilteredHorzSHCoeffsHeapIndex, mCbvSrvUavDescriptorSize),
			CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mFilteredHorzClearHeapIndex, mCbvSrvUavDescriptorSize),
			mFilteredHorzSHCoeffsBuffer[0].Get(), clearValues, 0, nullptr);

		for (int i = 0; i < 2; ++i)
		{
			mCommandList->ClearUnorderedAccessViewFloat(
				CD3DX12_GPU_DESCRIPTOR_HANDLE(
					mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mGBufferHeapIndex + i, mCbvSrvUavDescriptorSize),
				CD3DX12_CPU_DESCRIPTOR_HANDLE(
					mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mGBufferClearHeapIndex + i, mCbvSrvUavDescriptorSize),
				mGBuffer[i].Get(), clearValues, 0, nullptr);
		}
	}

	mCommandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void NormalMapApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void NormalMapApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void NormalMapApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void NormalMapApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f * dt);

	mCamera.UpdateViewMatrix();

	if (GetAsyncKeyState(VK_UP))
		mGridRitem->walk(5.0f * dt);

	if (GetAsyncKeyState(VK_DOWN))
		mGridRitem->walk(-5.0f * dt);

	if (GetAsyncKeyState(VK_LEFT))
		mGridRitem->strafe(-5.0f * dt);

	if (GetAsyncKeyState(VK_RIGHT))
		mGridRitem->strafe(5.0f * dt);

	if (GetAsyncKeyState('I') & 0x8000)
		mBoxRitem->walk(5.0f * dt);

	if (GetAsyncKeyState('K') & 0x8000)
		mBoxRitem->walk(-5.0f * dt);

	if (GetAsyncKeyState('J') & 0x8000)
		mBoxRitem->strafe(-5.0f * dt);

	if (GetAsyncKeyState('L') & 0x8000)
		mBoxRitem->strafe(5.0f * dt);
}

void NormalMapApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMMatrixScaling(e->Scale.x, e->Scale.y, e->Scale.z) * XMMatrixTranslation(e->Position.x, e->Position.y, e->Position.z);
			XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.LastFrameWorld, e->WorldMat);
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.InvWorld, XMMatrixTranspose(invWorld));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;
			objConstants.vertexOffset = e->vertexOffset;
			objConstants.objId = e->ObjCBIndex;

			// Update Object's world matrix.
			e->WorldMat = world;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void NormalMapApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for (auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.NormalMapIndex = mat->NormalSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void NormalMapApp::UpdateMainPassCB(const GameTimer& gt)
{
	for (auto& e : mRitemLayer[(int)RenderLayer::DiffuseRTTest])
	{
		XMMATRIX world = XMMatrixScaling(e->Scale.x, e->Scale.y, e->Scale.z) * XMMatrixTranslation(e->Position.x, e->Position.y, e->Position.z);
		XMMATRIX invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);

		// Update matrices for temporal filtering.
		if (e->GeoName == "model")
		{
			XMStoreFloat4x4(&mMainPassCB.LastFrameWorld1, XMMatrixTranspose(e->WorldMat));
			XMStoreFloat4x4(&mMainPassCB.InvWorld1, XMMatrixTranspose(invWorld));
		}
		else if (e->GeoName == "box")
		{
			XMStoreFloat4x4(&mMainPassCB.LastFrameWorld2, XMMatrixTranspose(e->WorldMat));
			XMStoreFloat4x4(&mMainPassCB.InvWorld2, XMMatrixTranspose(invWorld));
		}
		else if (e->GeoName == "grid")
		{
			XMStoreFloat4x4(&mMainPassCB.LastFrameWorld3, XMMatrixTranspose(e->WorldMat));
			XMStoreFloat4x4(&mMainPassCB.InvWorld3, XMMatrixTranspose(invWorld));
		}
	}

	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	mMainPassCB.LastFrameView = mMainPassCB.View;
	mMainPassCB.LastFrameProj = mMainPassCB.Proj;
	mMainPassCB.LastFrameViewProj = mMainPassCB.ViewProj;

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	if (mMainPassCB.indexOfSample <= 2000)
		mMainPassCB.indexOfSample++;
	else
		mMainPassCB.indexOfSample = 1;

	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void NormalMapApp::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",
		"tileDiffuseMap",
		"tileNormalMap",
		"defaultDiffuseMap",
		"defaultNormalMap",
		"skyCubeMap"
	};

	std::vector<std::wstring> texFilenames =
	{
		L"../Textures/bricks2.dds",
		L"../Textures/bricks2_nmap.dds",
		L"../Textures/tile.dds",
		L"../Textures/tile_nmap.dds",
		L"../Textures/white1x1.dds",
		L"../Textures/default_nmap.dds",
		L"../Textures/snowcube1024.dds"
	};

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		mTextures[texMap->Name] = std::move(texMap);
	}
}

void NormalMapApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 3, 0);

	// 9 RWTexture2D for screen space intermediate SH buffer
	CD3DX12_DESCRIPTOR_RANGE texTable2;
	texTable2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 9, 0, 1);

	// 9 RWTexture2D for screen space this frame SH buffer
	CD3DX12_DESCRIPTOR_RANGE texTable3;
	texTable3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 9, 0, 2);

	// 9 RWTexture2D for screen space last frame SH buffer
	CD3DX12_DESCRIPTOR_RANGE texTable4;
	texTable4.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 9, 0, 3);

	// 9 RWTexture2D for screen space this frame horizontal filtered SH buffer
	CD3DX12_DESCRIPTOR_RANGE texTable5;
	texTable5.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 9, 0, 4);

	// 9 RWTexture2D for screen space this frame filtered SH buffer
	CD3DX12_DESCRIPTOR_RANGE texTable6;
	texTable6.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 5);

	// 9 RWTexture2D for screen space this frame horizontal filtered SH buffer
	CD3DX12_DESCRIPTOR_RANGE texTable7;
	texTable7.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 9, 0, 6);

	// Root parameter can be a table, root descriptor or root constants.
	constexpr int parameterNum = 16;
	CD3DX12_ROOT_PARAMETER slotRootParameter[parameterNum];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[5].InitAsUnorderedAccessView(0);
	slotRootParameter[6].InitAsUnorderedAccessView(1);
	slotRootParameter[7].InitAsUnorderedAccessView(3);
	slotRootParameter[8].InitAsUnorderedAccessView(4);
	slotRootParameter[9].InitAsUnorderedAccessView(5); // last frame's object coeffs
	slotRootParameter[10].InitAsDescriptorTable(1, &texTable2, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[11].InitAsDescriptorTable(1, &texTable3, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[12].InitAsDescriptorTable(1, &texTable4, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[13].InitAsDescriptorTable(1, &texTable5, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[14].InitAsDescriptorTable(1, &texTable6, D3D12_SHADER_VISIBILITY_PIXEL); // G-Buffer 
	slotRootParameter[15].InitAsDescriptorTable(1, &texTable7, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(parameterNum, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void NormalMapApp::BuildDescriptorHeaps()
{
	//
	// Create the clear descriptor heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC clearHeapDesc = {};
	clearHeapDesc.NumDescriptors = 39;
	clearHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	clearHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&clearHeapDesc, IID_PPV_ARGS(&mClearDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// RWTexture2D for screen space coeffs
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = mIntermediateScreenSpaceSHCoeffsBuffer[0]->GetDesc().Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	mIntermediateClearHeapIndex = 0;
	for (int i = 0; i < 9; ++i)
	{
		md3dDevice->CreateUnorderedAccessView(mIntermediateScreenSpaceSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	mThisFrameClearHeapIndex = mIntermediateClearHeapIndex + 9;
	for (int i = 0; i < 9; ++i)
	{
		md3dDevice->CreateUnorderedAccessView(mThisFrameScreenSpaceSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	mFilteredHorzClearHeapIndex = mThisFrameClearHeapIndex + 9;
	for (int i = 0; i < 9; ++i)
	{
		md3dDevice->CreateUnorderedAccessView(mFilteredHorzSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	mGBufferClearHeapIndex = mFilteredHorzClearHeapIndex + 9;
	for (int i = 0; i < 2; ++i)
	{
		md3dDevice->CreateUnorderedAccessView(mGBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	mFilteredVertClearHeapIndex = mGBufferClearHeapIndex + 2;
	for (int i = 0; i < 9; ++i)
	{
		md3dDevice->CreateUnorderedAccessView(mFilteredVertSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	}

	//
	// Create the SRV/UAV descriptor heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 58;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap)));

	hDescriptor = mDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	std::vector<ComPtr<ID3D12Resource>> tex2DList =
	{
		mTextures["bricksDiffuseMap"]->Resource,
		mTextures["bricksNormalMap"]->Resource,
		mTextures["tileDiffuseMap"]->Resource,
		mTextures["tileNormalMap"]->Resource,
		mTextures["defaultDiffuseMap"]->Resource,
		mTextures["defaultNormalMap"]->Resource
	};

	auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	for (UINT i = 0; i < (UINT)tex2DList.size(); ++i)
	{
		srvDesc.Format = tex2DList[i]->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
		md3dDevice->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	}

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

	mSkyTexHeapIndex = (UINT)tex2DList.size();

	// depth map
	mDepthMapHeapIndex = mSkyTexHeapIndex + 1;
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	mDepthMap->BuildDescriptors(
		hDescriptor,
		CD3DX12_GPU_DESCRIPTOR_HANDLE(mDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), mDepthMapHeapIndex, mCbvSrvUavDescriptorSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart(), 1, mDsvDescriptorSize));

	// Texture space visibility4 buffer.
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = mTextureSpaceVisibilityBuffer->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = mTextureSpaceVisibilityBuffer->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(mTextureSpaceVisibilityBuffer.Get(), &srvDesc, hDescriptor);
	mTextureSpaceVisibility4SRVHeapIndex = mDepthMapHeapIndex + 1;

	// Acceleration structure.
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	D3D12_SHADER_RESOURCE_VIEW_DESC ASSrvDesc;
	ASSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	ASSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
	ASSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	ASSrvDesc.RaytracingAccelerationStructure.Location =
		m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
	// Write the acceleration structure view in the heap
	md3dDevice->CreateShaderResourceView(nullptr, &ASSrvDesc, hDescriptor);
	mAccelerationStructureHeapIndex = mTextureSpaceVisibility4SRVHeapIndex + 1;

	mScreenSpaceIntermediateSHCoeffsHeapIndex = mAccelerationStructureHeapIndex + 1;
	for (int i = 0; i < 9; ++i)
	{
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateUnorderedAccessView(mIntermediateScreenSpaceSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
	}

	mScreenSpaceThisFrameSHCoeffsHeapIndex = mScreenSpaceIntermediateSHCoeffsHeapIndex + 9;
	for (int i = 0; i < 9; ++i)
	{
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateUnorderedAccessView(mThisFrameScreenSpaceSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
	}

	mScreenSpaceLastFrameSHCoeffsHeapIndex = mScreenSpaceThisFrameSHCoeffsHeapIndex + 9;
	for (int i = 0; i < 9; ++i)
	{
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateUnorderedAccessView(mLastFrameScreenSpaceSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
	}

	mFilteredHorzSHCoeffsHeapIndex = mScreenSpaceLastFrameSHCoeffsHeapIndex + 9;
	for (int i = 0; i < 9; ++i)
	{
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateUnorderedAccessView(mFilteredHorzSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
	}

	mGBufferHeapIndex = mFilteredHorzSHCoeffsHeapIndex + 9;
	for (int i = 0; i < 2; ++i)
	{
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateUnorderedAccessView(mGBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
	}

	mFilteredVertSHCoeffsHeapIndex = mGBufferHeapIndex + 2;
	for (int i = 0; i < 9; ++i)
	{
		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateUnorderedAccessView(mFilteredVertSHCoeffsBuffer[i].Get(), nullptr, &uavDesc, hDescriptor);
	}

	// Texture space visibility4 buffer.
	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	uavDesc = {};
	uavDesc.Format = mTextureSpaceVisibilityBuffer->GetDesc().Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;
	md3dDevice->CreateUnorderedAccessView(mTextureSpaceVisibilityBuffer.Get(), nullptr, &uavDesc, hDescriptor);
	mTextureSpaceVisibility4UAVHeapIndex = mFilteredVertSHCoeffsHeapIndex + 9;
}

void NormalMapApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = d3dUtil::CompileShader(L"Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ProjEnvVS"] = d3dUtil::CompileShader(L"Shaders\\ProjEnv.hlsl", nullptr, "VS", "vs_5_1");

	mShaders["ProjLTVS"] = d3dUtil::CompileShader(L"Shaders\\ProjLTPerVertex.hlsl", nullptr, "VS", "vs_5_1");

	mShaders["ProjLTTextureVS"] = d3dUtil::CompileShader(L"Shaders\\ProjLTPerVertexTextureSpace.hlsl", nullptr, "VS", "vs_5_1");

	mShaders["ReconstructLightVS"] = d3dUtil::CompileShader(L"Shaders\\ReconstructLight.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ReconstructLightPS"] = d3dUtil::CompileShader(L"Shaders\\ReconstructLight.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ProjLTPixellVS"] = d3dUtil::CompileShader(L"Shaders\\ProjLTPerPixelNew.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ProjLTPixellPS"] = d3dUtil::CompileShader(L"Shaders\\ProjLTPerPixelNew.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["FilterVS"] = d3dUtil::CompileShader(L"Shaders\\Filter.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["FilterPS"] = d3dUtil::CompileShader(L"Shaders\\Filter.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["OutlierRemovalVS"] = d3dUtil::CompileShader(L"Shaders\\Outlier_removal.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["OutlierRemovalPS"] = d3dUtil::CompileShader(L"Shaders\\Outlier_removal.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["FilterHorzVS"] = d3dUtil::CompileShader(L"Shaders\\FilterHorizontal.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["FilterHorzPS"] = d3dUtil::CompileShader(L"Shaders\\FilterHorizontal.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["FilterVertVS"] = d3dUtil::CompileShader(L"Shaders\\FilterVertical.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["FilterVertPS"] = d3dUtil::CompileShader(L"Shaders\\FilterVertical.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["TemporalFilterVS"] = d3dUtil::CompileShader(L"Shaders\\TemporalFilter.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["TemporalFilterPS"] = d3dUtil::CompileShader(L"Shaders\\TemporalFilter.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["FilterHorzWorldVS"] = d3dUtil::CompileShader(L"Shaders\\FilterHorizontalWorld.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["FilterHorzWorldPS"] = d3dUtil::CompileShader(L"Shaders\\FilterHorizontalWorld.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["FilterVertWorldVS"] = d3dUtil::CompileShader(L"Shaders\\FilterVerticalWorld.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["FilterVertWorldPS"] = d3dUtil::CompileShader(L"Shaders\\FilterVerticalWorld.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ReconLightPixelVS"] = d3dUtil::CompileShader(L"Shaders\\FilterAndReconstructPerPixel.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ReconLightPixelPS"] = d3dUtil::CompileShader(L"Shaders\\FilterAndReconstructPerPixel.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["DepthVS"] = d3dUtil::CompileShader(L"Shaders\\Depth.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["DepthPS"] = d3dUtil::CompileShader(L"Shaders\\Depth.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["WriteGBufferVS"] = d3dUtil::CompileShader(L"Shaders\\WriteGBuffer.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["WriteGBufferPS"] = d3dUtil::CompileShader(L"Shaders\\WriteGBuffer.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void NormalMapApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 10);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(10.0f, 10.0f, 300, 300);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(-1.0f, 1.0f, 2.0f, 2.0f, 0.0f);

	GeometryGenerator::MeshData model = Model("Models/nanosuit/nanosuit.obj").CreateModel();

	auto geoBox = std::make_unique<MeshGeometry>();
	auto geoGrid = std::make_unique<MeshGeometry>();
	auto geoSphere = std::make_unique<MeshGeometry>();
	auto geoCylinder = std::make_unique<MeshGeometry>();
	auto geoModel = std::make_unique<MeshGeometry>();
	auto geoQuad = std::make_unique<MeshGeometry>();

	geoBox->Name = "box";
	geoGrid->Name = "grid";
	geoSphere->Name = "sphere";
	geoCylinder->Name = "cylinder";
	geoModel->Name = "model";
	geoQuad->Name = "quad";

	geoBox->VertexCount = box.Vertices.size();
	geoGrid->VertexCount = grid.Vertices.size();
	geoSphere->VertexCount = sphere.Vertices.size();
	geoCylinder->VertexCount = cylinder.Vertices.size();
	geoModel->VertexCount = model.Vertices.size();
	geoQuad->VertexCount = quad.Vertices.size();

	geoBox->IndexCount = box.Indices32.size();
	geoGrid->IndexCount = grid.Indices32.size();
	geoSphere->IndexCount = sphere.Indices32.size();
	geoCylinder->IndexCount = cylinder.Indices32.size();
	geoModel->IndexCount = model.Indices32.size();
	geoQuad->IndexCount = quad.Indices32.size();

	UINT vertexSize = sizeof(Vertex);
	UINT indexSize = sizeof(std::uint32_t);

	geoBox->VertexByteStride = sizeof(Vertex);
	geoBox->VertexBufferByteSize = geoBox->VertexCount * vertexSize;
	geoBox->IndexFormat = DXGI_FORMAT_R32_UINT;
	geoBox->IndexBufferByteSize = geoBox->IndexCount * indexSize;

	geoGrid->VertexByteStride = sizeof(Vertex);
	geoGrid->VertexBufferByteSize = geoGrid->VertexCount * vertexSize;
	geoGrid->IndexFormat = DXGI_FORMAT_R32_UINT;
	geoGrid->IndexBufferByteSize = geoGrid->IndexCount * indexSize;

	geoSphere->VertexByteStride = sizeof(Vertex);
	geoSphere->VertexBufferByteSize = geoSphere->VertexCount * vertexSize;
	geoSphere->IndexFormat = DXGI_FORMAT_R32_UINT;
	geoSphere->IndexBufferByteSize = geoSphere->IndexCount * indexSize;

	geoCylinder->VertexByteStride = sizeof(Vertex);
	geoCylinder->VertexBufferByteSize = geoCylinder->VertexCount * vertexSize;
	geoCylinder->IndexFormat = DXGI_FORMAT_R32_UINT;
	geoCylinder->IndexBufferByteSize = geoCylinder->IndexCount * indexSize;

	geoModel->VertexByteStride = sizeof(Vertex);
	geoModel->VertexBufferByteSize = geoModel->VertexCount * vertexSize;
	geoModel->IndexFormat = DXGI_FORMAT_R32_UINT;
	geoModel->IndexBufferByteSize = geoModel->IndexCount * indexSize;

	geoQuad->VertexByteStride = sizeof(Vertex);
	geoQuad->VertexBufferByteSize = geoQuad->VertexCount * vertexSize;
	geoQuad->IndexFormat = DXGI_FORMAT_R32_UINT;
	geoQuad->IndexBufferByteSize = geoQuad->IndexCount * indexSize;

	SubmeshGeometry submesh;
	submesh.BaseVertexLocation = 0;
	submesh.StartIndexLocation = 0;
	submesh.IndexCount = geoBox->IndexCount;
	submesh.VertexCount = geoBox->VertexCount;
	geoBox->DrawArgs["box"] = submesh;

	submesh.IndexCount = geoGrid->IndexCount;
	submesh.VertexCount = geoGrid->VertexCount;
	geoGrid->DrawArgs["grid"] = submesh;

	submesh.IndexCount = geoSphere->IndexCount;
	submesh.VertexCount = geoSphere->VertexCount;
	geoSphere->DrawArgs["sphere"] = submesh;

	submesh.IndexCount = geoCylinder->IndexCount;
	submesh.VertexCount = geoCylinder->VertexCount;
	geoCylinder->DrawArgs["cylinder"] = submesh;

	submesh.IndexCount = geoModel->IndexCount;
	submesh.VertexCount = geoModel->VertexCount;
	geoModel->DrawArgs["model"] = submesh;

	submesh.IndexCount = geoQuad->IndexCount;
	submesh.VertexCount = geoQuad->VertexCount;
	geoQuad->DrawArgs["quad"] = submesh;

	geoBox->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), box.Vertices.data(), box.Vertices.size() * vertexSize, geoBox->VertexBufferUploader);
	geoBox->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), box.Indices32.data(), box.Indices32.size() * indexSize, geoBox->IndexBufferUploader);

	geoGrid->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), grid.Vertices.data(), grid.Vertices.size() * vertexSize, geoGrid->VertexBufferUploader);
	geoGrid->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), grid.Indices32.data(), grid.Indices32.size() * indexSize, geoGrid->IndexBufferUploader);

	geoSphere->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), sphere.Vertices.data(), sphere.Vertices.size() * vertexSize, geoSphere->VertexBufferUploader);
	geoSphere->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), sphere.Indices32.data(), sphere.Indices32.size() * indexSize, geoSphere->IndexBufferUploader);

	geoCylinder->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), cylinder.Vertices.data(), cylinder.Vertices.size() * vertexSize, geoCylinder->VertexBufferUploader);
	geoCylinder->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), cylinder.Indices32.data(), cylinder.Indices32.size() * indexSize, geoCylinder->IndexBufferUploader);

	geoModel->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), model.Vertices.data(), model.Vertices.size() * vertexSize, geoModel->VertexBufferUploader);
	geoModel->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), model.Indices32.data(), model.Indices32.size() * indexSize, geoModel->IndexBufferUploader);

	geoQuad->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), quad.Vertices.data(), quad.Vertices.size() * vertexSize, geoQuad->VertexBufferUploader);
	geoQuad->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), quad.Indices32.data(), quad.Indices32.size() * indexSize, geoModel->IndexBufferUploader);

	mGeometries[geoBox->Name] = std::move(geoBox);
	mGeometries[geoGrid->Name] = std::move(geoGrid);
	mGeometries[geoSphere->Name] = std::move(geoSphere);
	mGeometries[geoCylinder->Name] = std::move(geoCylinder);
	mGeometries[geoModel->Name] = std::move(geoModel);
	mGeometries[geoQuad->Name] = std::move(geoQuad);
}

void NormalMapApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for sky.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;

	// The camera is inside the sky sphere, so just turn off culling.
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	//
	// PSO for projecting environment map pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC projEnvPsoDesc = opaquePsoDesc;
	projEnvPsoDesc.pRootSignature = mRootSignature.Get();
	projEnvPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["ProjEnvVS"]->GetBufferPointer()),
		mShaders["ProjEnvVS"]->GetBufferSize()
	};
	projEnvPsoDesc.PS =
	{
		nullptr,0
	};
	// projecting environment map pass does not have a render target.
	projEnvPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	projEnvPsoDesc.NumRenderTargets = 0;
	projEnvPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	projEnvPsoDesc.DepthStencilState.DepthEnable = false;
	projEnvPsoDesc.DepthStencilState.StencilEnable = false;
	projEnvPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&projEnvPsoDesc, IID_PPV_ARGS(&mPSOs["proj_env"])));

	//
	// PSO for reconstructing light.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC reconsteuctPsoDesc = opaquePsoDesc;
	reconsteuctPsoDesc.pRootSignature = mRootSignature.Get();
	reconsteuctPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	reconsteuctPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["ReconstructLightVS"]->GetBufferPointer()),
				mShaders["ReconstructLightVS"]->GetBufferSize()
	};
	reconsteuctPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["ReconstructLightPS"]->GetBufferPointer()),
				mShaders["ReconstructLightPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&reconsteuctPsoDesc, IID_PPV_ARGS(&mPSOs["reconstruct"])));

	//
	// PSO for projecting light transport per vertex.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC projLTPsoDesc = projEnvPsoDesc;
	projLTPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	projLTPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["ProjLTVS"]->GetBufferPointer()),
				mShaders["ProjLTVS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&projLTPsoDesc, IID_PPV_ARGS(&mPSOs["projLT"])));

	//
	// PSO for projecting light transport per vertex using screen space visibility4 buffer.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC projLTTextureSpacePsoDesc = projEnvPsoDesc;
	projLTTextureSpacePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	projLTTextureSpacePsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["ProjLTTextureVS"]->GetBufferPointer()),
				mShaders["ProjLTTextureVS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&projLTTextureSpacePsoDesc, IID_PPV_ARGS(&mPSOs["projLTTextureSpace"])));

	//
	// PSO for depth map pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC depthMapPsoDesc = opaquePsoDesc;
	depthMapPsoDesc.RasterizerState.DepthBias = 100000;
	depthMapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	depthMapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;

	depthMapPsoDesc.pRootSignature = mRootSignature.Get();
	depthMapPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["DepthVS"]->GetBufferPointer()),
		mShaders["DepthVS"]->GetBufferSize()
	};
	depthMapPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["DepthPS"]->GetBufferPointer()),
		mShaders["DepthPS"]->GetBufferSize()
	};
	// depth map pass does not have a render target.
	depthMapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	depthMapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&depthMapPsoDesc, IID_PPV_ARGS(&mPSOs["draw_depth"])));

	//
	// PSO for screen space projecting light transport
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC writeGBufferPsoDesc = opaquePsoDesc;
	writeGBufferPsoDesc.pRootSignature = mRootSignature.Get();
	writeGBufferPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	writeGBufferPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	writeGBufferPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["WriteGBufferVS"]->GetBufferPointer()),
				mShaders["WriteGBufferVS"]->GetBufferSize()
	};
	writeGBufferPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["WriteGBufferPS"]->GetBufferPointer()),
				mShaders["WriteGBufferPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&writeGBufferPsoDesc, IID_PPV_ARGS(&mPSOs["writeGBuffer"])));

	//
	// PSO for screen space projecting light transport
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC screenSpaceProjLTPsoDesc = opaquePsoDesc;
	screenSpaceProjLTPsoDesc.pRootSignature = mRootSignature.Get();
	screenSpaceProjLTPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	screenSpaceProjLTPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	screenSpaceProjLTPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	screenSpaceProjLTPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["ProjLTPixellVS"]->GetBufferPointer()),
				mShaders["ProjLTPixellVS"]->GetBufferSize()
	};
	screenSpaceProjLTPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["ProjLTPixellPS"]->GetBufferPointer()),
				mShaders["ProjLTPixellPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&screenSpaceProjLTPsoDesc, IID_PPV_ARGS(&mPSOs["screenSpaceProjLT"])));

	//
	// PSO for screen space filtering
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC screenSpaceFilterPsoDesc = opaquePsoDesc;
	screenSpaceFilterPsoDesc.pRootSignature = mRootSignature.Get();
	screenSpaceFilterPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	projEnvPsoDesc.DepthStencilState.DepthEnable = false;
	projEnvPsoDesc.DepthStencilState.StencilEnable = false;
	screenSpaceFilterPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	screenSpaceFilterPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterVS"]->GetBufferPointer()),
				mShaders["FilterVS"]->GetBufferSize()
	};
	screenSpaceFilterPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterPS"]->GetBufferPointer()),
				mShaders["FilterPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&screenSpaceFilterPsoDesc, IID_PPV_ARGS(&mPSOs["filter"])));

	//
	// PSO for temporal clamping.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC temporalClampPsoDesc = screenSpaceFilterPsoDesc;
	temporalClampPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["TemporalFilterVS"]->GetBufferPointer()),
				mShaders["TemporalFilterVS"]->GetBufferSize()
	};
	temporalClampPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["TemporalFilterPS"]->GetBufferPointer()),
				mShaders["TemporalFilterPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&temporalClampPsoDesc, IID_PPV_ARGS(&mPSOs["temporal_filter"])));

	//
	// PSO for temporal clamping.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC outlierRemovalPsoDesc = screenSpaceFilterPsoDesc;
	outlierRemovalPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["OutlierRemovalVS"]->GetBufferPointer()),
				mShaders["OutlierRemovalVS"]->GetBufferSize()
	};
	outlierRemovalPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["OutlierRemovalPS"]->GetBufferPointer()),
				mShaders["OutlierRemovalPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&outlierRemovalPsoDesc, IID_PPV_ARGS(&mPSOs["outlier_removal"])));

	//
	// PSO for screen space horizontal filtering 
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC screenSpaceFilterHorzPsoDesc = screenSpaceFilterPsoDesc;
	screenSpaceFilterHorzPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterHorzVS"]->GetBufferPointer()),
				mShaders["FilterHorzVS"]->GetBufferSize()
	};
	screenSpaceFilterHorzPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterHorzPS"]->GetBufferPointer()),
				mShaders["FilterHorzPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&screenSpaceFilterHorzPsoDesc, IID_PPV_ARGS(&mPSOs["filter_horz"])));

	//
	// PSO for screen space vertical filtering 
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC screenSpaceFilterVertPsoDesc = screenSpaceFilterPsoDesc;
	screenSpaceFilterVertPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterVertVS"]->GetBufferPointer()),
				mShaders["FilterVertVS"]->GetBufferSize()
	};
	screenSpaceFilterVertPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterVertPS"]->GetBufferPointer()),
				mShaders["FilterVertPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&screenSpaceFilterVertPsoDesc, IID_PPV_ARGS(&mPSOs["filter_vert"])));

	//
	// PSO for screen space horizontal filtering 
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC worldSpaceFilterHorzPsoDesc = screenSpaceFilterPsoDesc;
	worldSpaceFilterHorzPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterHorzWorldVS"]->GetBufferPointer()),
				mShaders["FilterHorzWorldVS"]->GetBufferSize()
	};
	worldSpaceFilterHorzPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterHorzWorldPS"]->GetBufferPointer()),
				mShaders["FilterHorzWorldPS"]->GetBufferSize()
	};
	worldSpaceFilterHorzPsoDesc.DepthStencilState.DepthEnable = false;
	worldSpaceFilterHorzPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&worldSpaceFilterHorzPsoDesc, IID_PPV_ARGS(&mPSOs["filter_horz_world"])));

	//
	// PSO for world space vertical filtering 
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC worldSpaceFilterVertPsoDesc = screenSpaceFilterPsoDesc;
	worldSpaceFilterVertPsoDesc.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterVertWorldVS"]->GetBufferPointer()),
				mShaders["FilterVertWorldVS"]->GetBufferSize()
	};
	worldSpaceFilterVertPsoDesc.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["FilterVertWorldPS"]->GetBufferPointer()),
				mShaders["FilterVertWorldPS"]->GetBufferSize()
	};
	worldSpaceFilterVertPsoDesc.DepthStencilState.DepthEnable = false;
	worldSpaceFilterVertPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&worldSpaceFilterVertPsoDesc, IID_PPV_ARGS(&mPSOs["filter_vert_world"])));

	//
	// PSO for screen space spatial filter and reconstructing light
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC screenSpaceReconstructLight = opaquePsoDesc;
	screenSpaceReconstructLight.pRootSignature = mRootSignature.Get();
	screenSpaceReconstructLight.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	projEnvPsoDesc.DepthStencilState.DepthEnable = false;
	projEnvPsoDesc.DepthStencilState.StencilEnable = false;
	screenSpaceReconstructLight.VS =
	{
				reinterpret_cast<BYTE*>(mShaders["ReconLightPixelVS"]->GetBufferPointer()),
				mShaders["ReconLightPixelVS"]->GetBufferSize()
	};
	screenSpaceReconstructLight.PS =
	{
				reinterpret_cast<BYTE*>(mShaders["ReconLightPixelPS"]->GetBufferPointer()),
				mShaders["ReconLightPixelPS"]->GetBufferSize()
	};
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&screenSpaceReconstructLight, IID_PPV_ARGS(&mPSOs["screenSpaceFilterAndRecon"])));
}

void NormalMapApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
	}
}

void NormalMapApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->NormalSrvHeapIndex = 1;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	bricks0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->NormalSrvHeapIndex = 3;
	tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	tile0->Roughness = 0.1f;

	auto mirror0 = std::make_unique<Material>();
	mirror0->Name = "mirror0";
	mirror0->MatCBIndex = 3;
	mirror0->DiffuseSrvHeapIndex = 4;
	mirror0->NormalSrvHeapIndex = 5;
	mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
	mirror0->Roughness = 0.1f;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 4;
	sky->DiffuseSrvHeapIndex = 6;
	sky->NormalSrvHeapIndex = 7;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["mirror0"] = std::move(mirror0);
	mMaterials["sky"] = std::move(sky);
}

void NormalMapApp::BuildRenderItems()
{
	auto skyRitem = std::make_unique<RenderItem>();
	skyRitem->Scale = { 5000.0f, 5000.0f, 5000.0f };
	skyRitem->WorldMat = XMMatrixScaling(5000.0f, 5000.0f, 5000.0f);
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 0;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["sphere"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	skyRitem->GeoName = "sphere";

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	auto modelRitem = std::make_unique<RenderItem>();
	modelRitem->Position = { 0.0f, 0.0f, 0.0f };
	modelRitem->Scale = { 0.3f, 0.3f, 0.3f };
	modelRitem->WorldMat = XMMatrixScaling(modelRitem->Scale.x, modelRitem->Scale.y, modelRitem->Scale.z);
	modelRitem->TexTransform = MathHelper::Identity4x4();
	modelRitem->ObjCBIndex = 1;
	modelRitem->Mat = mMaterials["bricks0"].get();
	modelRitem->Geo = mGeometries["model"].get();
	modelRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	modelRitem->IndexCount = modelRitem->Geo->DrawArgs["model"].IndexCount;
	modelRitem->StartIndexLocation = modelRitem->Geo->DrawArgs["model"].StartIndexLocation;
	modelRitem->BaseVertexLocation = modelRitem->Geo->DrawArgs["model"].BaseVertexLocation;
	modelRitem->GeoName = "model";
	modelRitem->vertexOffset = 0;

	mRitemLayer[(int)RenderLayer::DiffuseRT].push_back(modelRitem.get());
	mRitemLayer[(int)RenderLayer::DiffuseRTTest].push_back(modelRitem.get());
	mRitemLayer[(int)RenderLayer::BVH].push_back(modelRitem.get());
	mAllRitems.push_back(std::move(modelRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	boxRitem->Position = { 0.0f, 4.0f, 2.3f };
	boxRitem->Scale = { 1.5f, 1.5f, 1.5f };
	boxRitem->WorldMat = XMMatrixScaling(boxRitem->Scale.x, boxRitem->Scale.x, boxRitem->Scale.x) *
		XMMatrixTranslation(boxRitem->Position.x, boxRitem->Position.y, boxRitem->Position.z);
	boxRitem->TexTransform = MathHelper::Identity4x4();
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["tile0"].get();
	boxRitem->Geo = mGeometries["box"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	boxRitem->GeoName = "box";
	boxRitem->vertexOffset = mGeometries["model"]->VertexCount;

	mBoxRitem = boxRitem.get();

	mRitemLayer[(int)RenderLayer::DiffuseRTTest].push_back(boxRitem.get());
	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mRitemLayer[(int)RenderLayer::BVH].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->Position = { 0.5f, -2.0f, 2.5f };
	gridRitem->Scale = { 1.0f, 1.0f, 1.0f };
	gridRitem->WorldMat = XMMatrixTranslation(gridRitem->Position.x, gridRitem->Position.y, gridRitem->Position.z);
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 3;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["grid"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	gridRitem->GeoName = "grid";
	gridRitem->vertexOffset = mGeometries["model"]->VertexCount + mGeometries["box"]->VertexCount;

	mGridRitem = gridRitem.get();

	mRitemLayer[(int)RenderLayer::DiffuseRTTest].push_back(gridRitem.get());
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
	mRitemLayer[(int)RenderLayer::BVH].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));

	auto quadRitem = std::make_unique<RenderItem>();
	quadRitem->WorldMat = XMMatrixIdentity();
	quadRitem->TexTransform = MathHelper::Identity4x4();
	quadRitem->ObjCBIndex = 4;
	quadRitem->Mat = mMaterials["bricks0"].get();
	quadRitem->Geo = mGeometries["quad"].get();
	quadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quad"].IndexCount;
	quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
	quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;
	quadRitem->GeoName = "quad";

	mRitemLayer[(int)RenderLayer::Filter].push_back(quadRitem.get());
	mAllRitems.push_back(std::move(quadRitem));
}

void NormalMapApp::DrawRenderItemsIndexedInstanced(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> NormalMapApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void NormalMapApp::BuildSHCoeffsBuffer()
{
	//// Create the buffer that will be a UAV. 
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(sizeof(SHCoeff), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mEnvCoeffs)));

	int vertexCount = mGeometries["model"]->VertexCount + mGeometries["box"]->VertexCount + mGeometries["grid"]->VertexCount;
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexCount * sizeof(SHCoeff), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), // per-vertex
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mTemporalObjCoeffs)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexCount * sizeof(SHCoeff), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), // per-vertex
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mThisFrameObjCoeffs)));

	// Screen space buffer(9 RWTexture2D).
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mClientWidth;
	texDesc.Height = mClientHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// Screen space intermediate coeff buffer
	for (int i = 0; i < 9; ++i)
	{
		mIntermediateScreenSpaceSHCoeffsBuffer.emplace_back(nullptr);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&mIntermediateScreenSpaceSHCoeffsBuffer[i])));
	}

	// Screen space this frame coeff buffer
	for (int i = 0; i < 9; ++i)
	{
		mThisFrameScreenSpaceSHCoeffsBuffer.emplace_back(nullptr);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&mThisFrameScreenSpaceSHCoeffsBuffer[i])));
	}

	// Screen space last frame coeff buffer
	for (int i = 0; i < 9; ++i)
	{
		mLastFrameScreenSpaceSHCoeffsBuffer.emplace_back(nullptr);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&mLastFrameScreenSpaceSHCoeffsBuffer[i])));
	}

	// Screen space horizontal filtered coeff buffer
	for (int i = 0; i < 9; ++i)
	{
		mFilteredHorzSHCoeffsBuffer.emplace_back(nullptr);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&mFilteredHorzSHCoeffsBuffer[i])));
	}

	// Screen space vertical filtered coeff buffer
	for (int i = 0; i < 9; ++i)
	{
		mFilteredVertSHCoeffsBuffer.emplace_back(nullptr);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&mFilteredVertSHCoeffsBuffer[i])));
	}
}

void NormalMapApp::BuildVisibilityTermBuffer()
{
	int vertexCount = mGeometries["model"]->VertexCount; 
	vertexCount += mGeometries["box"]->VertexCount;
	vertexCount += mGeometries["grid"]->VertexCount;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(vertexCount * sizeof(XMFLOAT4X4), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mVisibilityBuffer)));

	// Texture space visibility4.
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = 512;
	texDesc.Height = 512;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mTextureSpaceVisibilityBuffer)));
}

void NormalMapApp::BuildRandomStateBuffer()
{
	//int vertexCount = mGeometries["model"]->VertexCount;
	//vertexCount += mGeometries["box"]->VertexCount;
	//vertexCount += mGeometries["grid"]->VertexCount;

	random_device rd;
	default_random_engine gen(rd());
	// intial state number must larger than 128.
	uniform_real_distribution<float> dis(0, 1);
	uniform_int_distribution<> dis2(130, (numeric_limits<int>::max)());
	vector<RandomState> intialStates(mClientWidth * mClientHeight);
	for (auto& state : intialStates)
	{
		state.z1 = dis2(gen);
		state.z2 = dis2(gen);
		state.z3 = dis2(gen);
		state.z4 = dis2(gen);
		state.u = dis(gen);
		state.v = dis(gen);
	}

	copySource = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), intialStates.data(), intialStates.size() * sizeof(RandomState), uploader);

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(copySource.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(intialStates.size() * sizeof(RandomState), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mRandomStateBuffer)
	));

	mCommandList->CopyResource(mRandomStateBuffer.Get(), copySource.Get());

	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mRandomStateBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
}

void NormalMapApp::BuildGBuffer()
{
	// G-Buffer (2 RWTexture2D).
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mClientWidth;
	texDesc.Height = mClientHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	for (int i = 0; i < 2; ++i)
	{
		mGBuffer.emplace_back(nullptr);
		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&mGBuffer[i])));
	}
}

void NormalMapApp::CheckRaytracingSupport()
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
	ThrowIfFailed(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
		&options5, sizeof(options5)));
	if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		throw std::runtime_error("Raytracing not supported on device");
}

//-----------------------------------------------------------------------------
//
// Create a bottom-level acceleration structure based on a list of vertex
// buffers in GPU memory along with their vertex count. The build is then done
// in 3 steps: gathering the geometry, computing the sizes of the required
// buffers, and building the actual AS
NormalMapApp::AccelerationStructureBuffers
NormalMapApp::CreateBottomLevelAS(
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers,
	std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vIndexBuffers)
{
	nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

	// Adding all vertex buffers and not transforming their position.
	for (size_t i = 0; i < vVertexBuffers.size(); i++) {
		// for (const auto &buffer : vVertexBuffers) {
		if (i < vIndexBuffers.size() && vIndexBuffers[i].second > 0)
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0,
				vVertexBuffers[i].second, sizeof(Vertex),
				vIndexBuffers[i].first.Get(), 0,
				vIndexBuffers[i].second, nullptr, 0, true);

		else
			bottomLevelAS.AddVertexBuffer(vVertexBuffers[i].first.Get(), 0,
				vVertexBuffers[i].second, sizeof(Vertex), 0,
				0);
	}

	// The AS build requires some scratch space to store temporary information.
	// The amount of scratch memory is dependent on the scene complexity.
	UINT64 scratchSizeInBytes = 0;
	// The final AS also needs to be stored in addition to the existing vertex
	// buffers. It size is also dependent on the scene complexity.
	UINT64 resultSizeInBytes = 0;

	bottomLevelAS.ComputeASBufferSizes(md3dDevice.Get(), false, &scratchSizeInBytes,
		&resultSizeInBytes);

	// Once the sizes are obtained, the application is responsible for allocating
	// the necessary buffers. Since the entire generation will be done on the GPU,
	// we can directly allocate those on the default heap
	AccelerationStructureBuffers buffers;
	buffers.pScratch = nv_helpers_dx12::CreateBuffer(
		md3dDevice.Get(), scratchSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
		nv_helpers_dx12::kDefaultHeapProps);
	buffers.pResult = nv_helpers_dx12::CreateBuffer(
		md3dDevice.Get(), resultSizeInBytes,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nv_helpers_dx12::kDefaultHeapProps);

	// Build the acceleration structure. Note that this call integrates a barrier
	// on the generated AS, so that it can be used to compute a top-level AS right
	// after this method.
	bottomLevelAS.Generate(mCommandList.Get(), buffers.pScratch.Get(),
		buffers.pResult.Get(), false, nullptr);

	return buffers;
}

//-----------------------------------------------------------------------------
// Create the main acceleration structure that holds all instances of the scene.
// Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
// the instances, computing the memory requirements for the AS, and building the
// AS itself
//
void NormalMapApp::CreateTopLevelAS(
	const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>
	& instances, // pair of bottom level AS and matrix of the instance
// #DXR Extra - Refitting
bool updateOnly // If true the top-level AS will only be refitted and not
				// rebuilt from scratch
) {

	// #DXR Extra - Refitting
	if (!updateOnly) {
		// Gather all the instances into the builder helper
		for (size_t i = 0; i < instances.size(); i++)
			m_topLevelASGenerator.AddInstance(instances[i].first.Get(), instances[i].second, static_cast<UINT>(i), static_cast<UINT>(2 * i));

		// As for the bottom-level AS, the building the AS requires some scratch
		// space to store temporary data in addition to the actual AS. In the case
		// of the top-level AS, the instance descriptors also need to be stored in
		// GPU memory. This call outputs the memory requirements for each (scratch,
		// results, instance descriptors) so that the application can allocate the
		// corresponding memory
		UINT64 scratchSize, resultSize, instanceDescsSize;

		m_topLevelASGenerator.ComputeASBufferSizes(
			md3dDevice.Get(), true, &scratchSize, &resultSize, &instanceDescsSize);

		// Create the scratch and result buffers. Since the build is all done on
		// GPU, those can be allocated on the default heap
		m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(
			md3dDevice.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nv_helpers_dx12::kDefaultHeapProps);
		m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(
			md3dDevice.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nv_helpers_dx12::kDefaultHeapProps);

		// The buffer describing the instances: ID, shader binding information,
		// matrices ... Those will be copied into the buffer by the helper through
		// mapping, so the buffer has to be allocated on the upload heap.
		m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(
			md3dDevice.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	}
	// After all the buffers are allocated, or if only an update is required, we
	// can build the acceleration structure. Note that in the case of the update
	// we also pass the existing AS as the 'previous' AS, so that it can be
	// refitted in place.
	m_topLevelASGenerator.Generate(mCommandList.Get(),
		m_topLevelASBuffers.pScratch.Get(),
		m_topLevelASBuffers.pResult.Get(),
		m_topLevelASBuffers.pInstanceDesc.Get(),
		updateOnly, m_topLevelASBuffers.pResult.Get());
}

void NormalMapApp::BuildAccelerationStructure()
{
	MeshGeometry* grid = mGeometries["grid"].get();
	m_bottomLevelASBuffers["grid"] = CreateBottomLevelAS(
		{ { grid->VertexBufferGPU, grid->VertexCount } },
		{ { grid->IndexBufferGPU, grid->IndexCount } }
	).pResult;

	MeshGeometry* model = mGeometries["model"].get();
	m_bottomLevelASBuffers["model"] = CreateBottomLevelAS(
		{ { model->VertexBufferGPU, model->VertexCount } },
		{ { model->IndexBufferGPU, model->IndexCount } }
	).pResult;

	MeshGeometry* box = mGeometries["box"].get();
	m_bottomLevelASBuffers["box"] = CreateBottomLevelAS(
		{ { box->VertexBufferGPU, box->VertexCount } },
		{ { box->IndexBufferGPU, box->IndexCount } }
	).pResult;

	for (const auto& renderItem : mRitemLayer[(int)RenderLayer::BVH])
	{
		std::string geoName = renderItem->GeoName;
		m_instances.push_back({ m_bottomLevelASBuffers[geoName], renderItem->WorldMat });
	}

	CreateTopLevelAS(m_instances);
}

//-----------------------------------------------------------------------------
// The ray generation shader needs to access 5 resources
//
ComPtr<ID3D12RootSignature> NormalMapApp::CreateRayGenSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;

	if (mProjLTSpace != Space::ScreenSpace)
	{
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV, 1); // Vertex buffer
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 0); // Object Constant buffer
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_CBV, 1); // Pass Constant buffer
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_UAV, 1); // RWStructured buffer(visibility)
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_UAV, 2); // RWStructured buffer(random state)
		rsc.AddHeapRangesParameter({
			{3 /*u3*/, 1/*1descriptor*/, 0/*space0*/, D3D12_DESCRIPTOR_RANGE_TYPE_UAV /*RWTexture2D visibility4*/,
				mTextureSpaceVisibility4UAVHeapIndex/*heap slot*/},
			{0 /*t0*/, 1/*1descriptor*/, 0/*space0*/, D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/,
				mAccelerationStructureHeapIndex/*heap slot*/}
			});
	}
	else
	{
		rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_UAV, 1); // RWStructured buffer(random state)
		rsc.AddHeapRangesParameter({
			{0 /*u0*/, 1/*1descriptor*/, 0/*space0*/, D3D12_DESCRIPTOR_RANGE_TYPE_UAV /*RWTexture2D visibility4*/,
				mScreenSpaceThisFrameSHCoeffsHeapIndex + 8/*heap slot*/},
			{2 /*u2*/, 2/*2descriptor*/, 0/*space0*/, D3D12_DESCRIPTOR_RANGE_TYPE_UAV /*RWTexture2D GBuffer positions*/,
				mGBufferHeapIndex/*heap slot*/},
			{0 /*t0*/, 1/*1descriptor*/, 0/*space0*/, D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure*/,
				mAccelerationStructureHeapIndex/*heap slot*/}
			});
	}

	return rsc.Generate(md3dDevice.Get(), true);
}

//-----------------------------------------------------------------------------
// The hit shader communicates only through the ray payload, and therefore does
// not require any resources
//
ComPtr<ID3D12RootSignature> NormalMapApp::CreateHitSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(md3dDevice.Get(), true);
}

//-----------------------------------------------------------------------------
// The hit shader communicates only through the ray payload, and therefore does
// not require any resources
//
ComPtr<ID3D12RootSignature> NormalMapApp::CreateMissSignature()
{
	nv_helpers_dx12::RootSignatureGenerator rsc;
	return rsc.Generate(md3dDevice.Get(), true);
}

//-----------------------------------------------------------------------------
//
// The raytracing pipeline binds the shader code, root signatures and pipeline
// characteristics in a single structure used by DXR to invoke the shaders and
// manage temporary memory during raytracing
//
//
void NormalMapApp::CreateRaytracingPipeline()
{
	nv_helpers_dx12::RayTracingPipelineGenerator pipeline(md3dDevice.Get());

	// The pipeline contains the DXIL code of all the shaders potentially executed
	// during the raytracing process. This section compiles the HLSL code into a
	// set of DXIL libraries. We chose to separate the code in several libraries
	// by semantic (ray generation, hit, miss) for clarity. Any code layout can be
	// used.

	if (mProjLTSpace == Space::ScreenSpace)
		m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\RayGenPerPixel.hlsl");
	else
		m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\RayGen.hlsl");
	m_textureSpaceRayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\TextureSpaceRayGen.hlsl");
	m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\Miss.hlsl");
	m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Shaders\\Hit.hlsl");

	// In a way similar to DLLs, each library is associated with a number of exported symbols. This
	// has to be done explicitly in the lines below. Note that a single library
	// can contain an arbitrary number of symbols, whose semantic is given in HLSL
	// using the [shader("xxx")] syntax
	if (mProjLTSpace == Space::TextureSpace)
		pipeline.AddLibrary(m_textureSpaceRayGenLibrary.Get(), { L"RayGen" });
	else
		pipeline.AddLibrary(m_rayGenLibrary.Get(), { L"RayGen" });
	pipeline.AddLibrary(m_missLibrary.Get(), { L"Miss" });
	pipeline.AddLibrary(m_hitLibrary.Get(), { L"ClosestHit" });

	// To be used, each DX12 shader needs a root signature defining which parameters and buffers will be accessed.
	m_rayGenSignature = CreateRayGenSignature();
	m_missSignature = CreateMissSignature();
	m_hitSignature = CreateHitSignature();

	pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");

	// The following section associates the root signature to each shader. Note
	// that we can explicitly show that some shaders share the same root signature
	// (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
	// to as hit groups, meaning that the underlying intersection, any-hit and
	// closest-hit shaders share the same root signature.
	pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), { L"RayGen" });
	pipeline.AddRootSignatureAssociation(m_missSignature.Get(), { L"Miss" });
	pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), { L"HitGroup" });

	// The payload size defines the maximum size of the data carried by the rays,
	// ie. the the data
	// exchanged between shaders, such as the HitInfo structure in the HLSL code.
	// It is important to keep this value as low as possible as a too high value
	// would result in unnecessary memory consumption and cache trashing.
	pipeline.SetMaxPayloadSize(sizeof(int)); // visibility term

	// Upon hitting a surface, DXR can provide several attributes to the hit. In
	// our sample we just use the barycentric coordinates defined by the weights
	// u,v of the last two vertices of the triangle. The actual barycentrics can
	// be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
	pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates

	  // The raytracing process can shoot rays from existing hit points, resulting
	// in nested TraceRay calls. Our sample code traces only primary rays, which
	// then requires a trace depth of 1. Note that this recursion depth should be
	// kept to a minimum for best performance. Path tracing algorithms can be
	// easily flattened into a simple loop in the ray generation.
	pipeline.SetMaxRecursionDepth(1);

	// Compile the pipeline for execution on the GPU
	m_rtStateObject = pipeline.Generate();

	// Cast the state object into a properties object, allowing to later access the shader pointers by name
	ThrowIfFailed(m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));
}

//-----------------------------------------------------------------------------
//
// The Shader Binding Table (SBT) is the cornerstone of the raytracing setup:
// this is where the shader resources are bound to the shaders, in a way that
// can be interpreted by the raytracer on GPU. In terms of layout, the SBT
// contains a series of shader IDs with their resource pointers. The SBT
// contains the ray generation shader, the miss shaders, then the hit groups.
// Using the helper class, those can be specified in arbitrary order.
//
void NormalMapApp::CreateShaderBindingTable(int diffuseRTIndex)
{
	// The SBT helper class collects calls to Add*Program.  If called several
	// times, the helper must be emptied before re-adding shaders.
	m_sbtHelper.Reset();

	// The pointer to the beginning of the heap is the only parameter required by
	// shaders without root parameters
	D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
		mDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

	// The helper treats both root parameter pointers and heap pointers as void*,
	// while DX12 uses the
	// D3D12_GPU_DESCRIPTOR_HANDLE to define heap pointers. The pointer in this
	// struct is a UINT64, which then has to be reinterpreted as a pointer.
	auto heapPointer = reinterpret_cast<UINT64*>(srvUavHeapHandle.ptr);

	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + 
		mRitemLayer[(int)RenderLayer::DiffuseRTTest][diffuseRTIndex]->ObjCBIndex * objCBByteSize;

	D3D12_GPU_VIRTUAL_ADDRESS vertexAdress;
	if (diffuseRTIndex == 0)
		vertexAdress = mGeometries["model"].get()->VertexBufferGPU->GetGPUVirtualAddress();
	else if (diffuseRTIndex == 1)
		vertexAdress = mGeometries["box"].get()->VertexBufferGPU->GetGPUVirtualAddress();
	else if (diffuseRTIndex == 2)
		vertexAdress = mGeometries["grid"].get()->VertexBufferGPU->GetGPUVirtualAddress();
	else
		throw std::runtime_error("wrong diffuseRTIndex");


	if (mProjLTSpace == Space::ScreenSpace)
	{
		m_sbtHelper.AddRayGenerationProgram(L"RayGen", { 
			(void*)mRandomStateBuffer->GetGPUVirtualAddress(),
			heapPointer 
			});
	}
	else
	{
		m_sbtHelper.AddRayGenerationProgram(L"RayGen", {
			(void*)vertexAdress,
			(void*)objCBAddress,
			(void*)mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress(),
			(void*)mVisibilityBuffer->GetGPUVirtualAddress(),
			(void*)mRandomStateBuffer->GetGPUVirtualAddress(),
			heapPointer
			});
	}

	// The miss and hit shaders do not access any external resources: instead they
	// communicate their results through the ray payload
	m_sbtHelper.AddMissProgram(L"Miss", { });
	m_sbtHelper.AddHitGroup(L"HitGroup", { });

	// Compute the size of the SBT given the number of shaders and their
	// parameters
	uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

	// Create the SBT on the upload heap. This is required as the helper will use
	// mapping to write the SBT contents. After the SBT compilation it could be
	// copied to the default heap for performance.
	m_sbtStorage = nv_helpers_dx12::CreateBuffer(
		md3dDevice.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
	if (!m_sbtStorage) {
		throw std::logic_error("Could not allocate the shader binding table");
	}
	// Compile the SBT from the shader and parameters info
	m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
}

void NormalMapApp::CalcVisibilityTerm()
{
	// #DXR - Refitting
	// Refit the top-level acceleration structure to account for the new transform matrix of the triangle. 
	CreateTopLevelAS(m_instances, true);	

	const std::array<UINT, 3> widths { mGeometries["model"]->VertexCount, mGeometries["box"]->VertexCount, mGeometries["grid"]->VertexCount };

	for (int i = 0; i < 3; ++i)
	{
		CreateShaderBindingTable(i);

		// Setup the raytracing task
		D3D12_DISPATCH_RAYS_DESC desc = {};
		// The layout of the SBT is as follows: ray generation shader, miss
		// shaders, hit groups. As described in the CreateShaderBindingTable method,
		// all SBT entries of a given type have the same size to allow a fixed
		// stride.

		// The ray generation shaders are always at the beginning of the SBT.
		uint32_t rayGenerationSectionSizeInBytes =
			m_sbtHelper.GetRayGenSectionSize();
		desc.RayGenerationShaderRecord.StartAddress =
			m_sbtStorage->GetGPUVirtualAddress();
		desc.RayGenerationShaderRecord.SizeInBytes =
			rayGenerationSectionSizeInBytes;

		// The miss shaders are in the second SBT section, right after the ray
		// generation shader. We have one miss shader. 
		uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
		desc.MissShaderTable.StartAddress =
			m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
		desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
		desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

		// The hit groups section start after the miss shaders. In this sample we
		// have one 1 hit group for the triangle
		uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
		desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() +
			rayGenerationSectionSizeInBytes + missSectionSizeInBytes;
		desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
		desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();

		if (mProjLTSpace == Space::ScreenSpace)
		{
			desc.Width = mClientWidth;
			desc.Height = mClientHeight;
			desc.Depth = 1;
			// Bind the raytracing pipeline
			mCommandList->SetPipelineState1(m_rtStateObject.Get());
			// Dispatch the rays and write to the raytracing output
			mCommandList->DispatchRays(&desc);
			break;
		}
		else
		{
			desc.Width = widths[i];
			desc.Height = 1;
			desc.Depth = 1;
			// Bind the raytracing pipeline
			mCommandList->SetPipelineState1(m_rtStateObject.Get());
			// Dispatch the rays and write to the raytracing output
			mCommandList->DispatchRays(&desc);
		}
	}
}

void NormalMapApp::DrawRenderItemsInstanced(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		MeshGeometry* mesh = ri->Geo;

		cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBufferView());
		cmdList->IASetIndexBuffer(nullptr);
		cmdList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

		cmdList->DrawInstanced(mesh->VertexCount, 1, ri->BaseVertexLocation, 0);
	}
}

void NormalMapApp::DrawSceneToDepthMap()
{
	mCommandList->RSSetViewports(1, &mDepthMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mDepthMap->ScissorRect());

	// Change to DEPTH_WRITE.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Clear the depth buffer.
	mCommandList->ClearDepthStencilView(mDepthMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Set null render target because we are only going to draw to
	// depth buffer.  Setting a null render target will disable color writes.
	// Note the active PSO also must specify a render target count of 0.
	mCommandList->OMSetRenderTargets(0, nullptr, false, &mDepthMap->Dsv());

	// Bind the pass constant buffer for the shadow map pass.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	mCommandList->SetPipelineState(mPSOs["draw_depth"].Get());

	DrawRenderItemsIndexedInstanced(mCommandList.Get(), mRitemLayer[(int)RenderLayer::DiffuseRTTest]);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}