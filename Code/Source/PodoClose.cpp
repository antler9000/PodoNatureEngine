#define NOMINMAX
#include "Podo.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <windows.h>

void Podo::Close()
{
	FlushCommandQueue();

	CloseFenceEvent();
	CloseImGui();
}

void Podo::CloseImGui()
{
	if (m_imGuiInitialized == false)
	{
		return;
	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	m_imGuiDescriptorHeapAllocator.Destroy();

	m_imGuiInitialized = false;
}

void Podo::CloseFenceEvent()
{
	CloseHandle(m_fenceEvent);
	m_fenceEvent = nullptr;
}