#define _CRT_SECURE_NO_WARNINGS

#include "shared.h"
#include <stdio.h>
#include "imgui.h"

void impl::showExampleWindow(const char* comment)
{
	char buffer[128];
	::memset(buffer, 0, 128);
	::sprintf(buffer, "Kiero Dear ImGui Example (%s)", comment);

	ImGui::Begin(buffer);

	ImGui::Text("Hello");
	ImGui::Button("World!");

	ImGui::End();
}