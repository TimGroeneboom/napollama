#include "ollamademoapp.h"

// External Includes
#include <utility/fileutils.h>
#include <nap/logger.h>
#include <inputrouter.h>
#include <rendergnomoncomponent.h>
#include <perspcameracomponent.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include "imgui_internal.h"

RTTI_BEGIN_CLASS_NO_DEFAULT_CONSTRUCTOR(nap::ollamademoApp)
	RTTI_CONSTRUCTOR(nap::Core&)
RTTI_END_CLASS

namespace nap 
{
	/**
	 * Initialize all the resources and instances used for drawing
	 * slowly migrating all functionality to NAP
	 */
	bool ollamademoApp::init(utility::ErrorState& error)
	{
		// Retrieve services
		mRenderService	= getCore().getService<nap::RenderService>();
		mSceneService	= getCore().getService<nap::SceneService>();
		mInputService	= getCore().getService<nap::InputService>();
		mGuiService		= getCore().getService<nap::IMGuiService>();

		// Fetch the resource manager
		mResourceManager = getCore().getResourceManager();

		// Get the render window
		mRenderWindow = mResourceManager->findObject<nap::RenderWindow>("Window");
		if (!error.check(mRenderWindow != nullptr, "unable to find render window with name: %s", "Window"))
			return false;

		// Get the scene that contains our entities and components
		mScene = mResourceManager->findObject<Scene>("Scene");
		if (!error.check(mScene != nullptr, "unable to find scene with name: %s", "Scene"))
			return false;

		// Get the camera entity
		mCameraEntity = mScene->findEntity("CameraEntity");
		if (!error.check(mCameraEntity != nullptr, "unable to find entity with name: %s", "CameraEntity"))
			return false;

		// Get the Gnomon entity
		mGnomonEntity = mScene->findEntity("GnomonEntity");
		if (!error.check(mGnomonEntity != nullptr, "unable to find entity with name: %s", "GnomonEntity"))
			return false;

		// Get the ollama chat device
		mOllamaChat = mResourceManager->findObject<OllamaChat>("OllamaChat");
		if (!error.check(mOllamaChat != nullptr, "unable to find OllamaChat device with name: %s", "OllamaChat"))
			return false;

		// All done!
		return true;
	}


	void ollamademoApp::onResponse(const std::string& response)
	{
		mTaskQueue.enqueue([this, response]()
		{
			std::string answer = mAnswer + response;
			if (ImGui::CalcTextSize(answer.c_str()).x > 880)
				mAnswer += "\n" + response;
			else
				mAnswer = answer;
		});
	}


	void ollamademoApp::onComplete()
	{
		mResponseComplete = true;
	}


	void ollamademoApp::onError(const std::string& error)
	{
		nap::Logger::error("Error: %s", error.c_str());
		mResponseComplete = true;
	}


	// Update app
	void ollamademoApp::update(double deltaTime)
	{
		std::function<void()> task;
		while (mTaskQueue.try_dequeue(task))
		{
			task();
		}

		// Use a default input router to forward input events (recursively) to all input components in the default scene
		nap::DefaultInputRouter input_router(true);
		mInputService->processWindowEvents(*mRenderWindow, input_router, { &mScene->getRootEntity() });

		if (ImGui::Begin("Ollama Chat"))
		{
			bool disabled = !mResponseComplete;
			if (disabled)
			{
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}

			ImGui::InputText("Question", &mQuestion);
			if (ImGui::Button("Ask"))
			{
				mResponseComplete = false;
				mAnswer = "";
				mOllamaChat->chat(mQuestion,
								  [this](const std::string& response){ onResponse(response); },
								  [this](){ onComplete(); },
								  [this](const std::string& error){ onError(error); });
			}

			if (disabled)
			{
				ImGui::PopItemFlag();
				ImGui::PopStyleVar();
			}

			disabled = mResponseComplete;

			if (disabled)
			{
				ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
			}

			ImGui::SameLine();
			if (ImGui::Button("Stop"))
			{
				mOllamaChat->stopResponse();
			}

			if (disabled)
			{
				ImGui::PopItemFlag();
				ImGui::PopStyleVar();
			}

			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

			ImGui::InputTextMultiline("AI Response", &mAnswer, ImVec2(900, 1200), ImGuiInputTextFlags_ReadOnly);

			ImGui::PopItemFlag();
		}

		ImGui::End();
	}
	
	
	// Render app
	void ollamademoApp::render()
	{
		// Signal the beginning of a new frame, allowing it to be recorded.
		// The system might wait until all commands that were previously associated with the new frame have been processed on the GPU.
		// Multiple frames are in flight at the same time, but if the graphics load is heavy the system might wait here to ensure resources are available.
		mRenderService->beginFrame();

		// Begin recording the render commands for the main render window
		if (mRenderService->beginRecording(*mRenderWindow))
		{
			// Begin render pass
			mRenderWindow->beginRendering();

			// Get Perspective camera to render with
			auto& perp_cam = mCameraEntity->getComponent<PerspCameraComponentInstance>();

			// Add Gnomon
			std::vector<nap::RenderableComponentInstance*> components_to_render
			{
				&mGnomonEntity->getComponent<RenderGnomonComponentInstance>()
			};

			// Render Gnomon
			mRenderService->renderObjects(*mRenderWindow, perp_cam, components_to_render);

			// Render GUI elements
			mGuiService->draw();

			// Stop render pass
			mRenderWindow->endRendering();

			// End recording
			mRenderService->endRecording();
		}

		// Proceed to next frame
		mRenderService->endFrame();
	}
	

	void ollamademoApp::windowMessageReceived(WindowEventPtr windowEvent)
	{
		mRenderService->addEvent(std::move(windowEvent));
	}
	
	
	void ollamademoApp::inputMessageReceived(InputEventPtr inputEvent)
	{
		if (inputEvent->get_type().is_derived_from(RTTI_OF(nap::KeyPressEvent)))
		{
			// If we pressed escape, quit the loop
			nap::KeyPressEvent* press_event = static_cast<nap::KeyPressEvent*>(inputEvent.get());
			if (press_event->mKey == nap::EKeyCode::KEY_ESCAPE)
				quit();

			// f is pressed, toggle full-screen
			if (press_event->mKey == nap::EKeyCode::KEY_f)
				mRenderWindow->toggleFullscreen();
		}
		// Add event, so it can be forwarded on update
		mInputService->addEvent(std::move(inputEvent));
	}

	
	int ollamademoApp::shutdown()
	{
		return 0;
	}

}
