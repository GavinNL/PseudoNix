#define IMGUI_DEFINE_MATH_OPERATORS
#include "App.h"

//#define PSEUDONIX_LOG_LEVEL_INFO
#include <PseudoNix/ImGuiTerminal.h>
#include "common_setup.h"

#include <SDL_main.h>

struct MyApplication : public ImGuiApplication
{
public:
    PseudoNix::System       m_mini;

    ~MyApplication()
    {
        // Make sure all processes are shutdown
        m_mini.destroy();
    }

    MyApplication()
    {
        // set up the system
        setup_functions(m_mini);

        // an ImGui Terminal function has been created if you want to
        // add a terminal to your projects. It is fairly simple
        // and can be copied/modified for extra features
        m_mini.setFunction("term", "Opens a new ImGui Terminal", PseudoNix::terminalWindow_coro);

        m_mini.setFunction("theme", "Sets the ImGui Theme", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() > 1)
            {
                if(ARGS[1] == "classic")
                {
                    ImGui::StyleColorsClassic();
                }
                if(ARGS[1] == "light")
                {
                    ImGui::StyleColorsLight();
                }
                if(ARGS[1] == "dark")
                {
                    ImGui::StyleColorsDark();
                }
            }
            else
            {
                COUT << "theme [classic|dark|light]\n";
            }
            co_return 0;
        });

        m_mini.setFunction("confirm", "Example dialog box", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            int frameCount[2] = {ImGui::GetFrameCount()-1, ImGui::GetFrameCount()-1};
            bool quit = false;
            int ret = 0;

            std::string txt = ARGS.size() == 1 ? std::format("{}", "Confirm?") : std::format("{}", ARGS[1]);

            while(!quit)
            {
                frameCount[0] = frameCount[1];
                frameCount[1] = ImGui::GetFrameCount();

                if(frameCount[0] != frameCount[1])
                {
                    ImGui::Begin("Confirm:");
                    ImGui::TextWrapped("%s", txt.c_str());
                    if(ImGui::Button("Confirm"))
                    {
                        ret = 0;
                        quit=true;
                    }
                    ImGui::SameLine();
                    if(ImGui::Button("Cancel"))
                    {
                        ret = 1;
                        quit = true;
                    }
                    ImGui::End();
                }
                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
            }

            // returns exit code 0 if confirmed and 1 if cancelled
            co_return static_cast<int>(ret);
        });

        m_mini.spawnProcess({"term", "sh"});
        m_mini.spawnProcess({"bgrunner", "THREADPOOL"});
    }

    void imguiPreFrame()
    {
        // Lets execute the main task queue first
        // This is only an example
        m_mini.taskQueueExecute("PRE_MAIN");
    }

    void imguiPostFrame()
    {
        m_mini.taskQueueExecute("POST_MAIN");
    }
    void imguiRender()
    {
        // Each time imgui performs the rendering
        // we are going to execute the scheduler and invoke
        // all coroutines once. Because we are running the
        // coroutines within the imguiRender() functions
        // the coroutines can also draw ImGui objects
        m_mini.taskQueueExecute("MAIN", std::chrono::milliseconds(1), 15);
    }
};


int main(int argc, char* argv[])
{
    //(void)argc;
    //(void)argv;
    return ImGuiApp::run<MyApplication>("PseudoNix ImGui Terminal Example", 1920, 1080);
}

#include <imgui_impl_sdl2.cpp>
#include <imgui_impl_sdlrenderer2.cpp>

#include <src/imgui_demo.cpp>
#include <src/imgui_widgets.cpp>
#include <src/imgui_draw.cpp>
#include <src/imgui_tables.cpp>
#include <src/imgui.cpp>
#include <imgui_stdlib.cpp>

