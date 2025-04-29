#define IMGUI_DEFINE_MATH_OPERATORS
#include "App.h"

//#define PSUEDONIX_ENABLE_DEBUG
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/ImGuiTerminal.h>

#include <SDL_main.h>

struct MyApplication : public ImGuiApplication
{
public:
    PseudoNix::System       m_mini;

    void setupFunctions()
    {

        // The sh function is provided for you.
        // It's relatively rudametry but allowed you do
        // do simple linux pipling and shell substitution
        //
        // cmd1 | cmd2
        // cmd1 && cmd2
        // cmd1 || cmd2
        // echo "Hello ${USER}"
        //
        m_mini.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, PseudoNix::ShellEnv{}));

        // an ImGui Terminal function has been created if you want to
        // add a terminal to your projects. It is fairly simple
        // and can be copied/modified for extra features
        m_mini.setFunction("term", PseudoNix::terminalWindow_coro);

        m_mini.setFunction("theme", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
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

        m_mini.setFunction("confirm", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
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
                co_await ctrl->await_yield();
            }

            // returns exit code 0 if confirmed and 1 if cancelled
            co_return static_cast<int>(ret);
        });
    }


    MyApplication()
    {
        // This is the pre-exec function that gets called
        // right before the coroutine is first executed
        //
        // It is used to modify the arguments
        //
        // You can use it to modify the args or add new
        // data such as environment variables
        m_mini.m_preExec = [](PseudoNix::System::Exec & E)
        {
            const char* username = std::getenv(
                #ifdef _WIN32
                "USERNAME"
                #else
                "USER"
                #endif
                );
            if (username) {
                E.env["USER"] = username;
            }
        };

        // Set up additional commands we want
        // in our System
        setupFunctions();

        // Createa  new task queue called "THREAD"
        // This can be executed at a different
        // time as the MAIN task queue.
        m_mini.taskQueueCreate("PRE_MAIN");
        m_mini.taskQueueCreate("POST_MAIN");
        m_mini.taskQueueCreate("THREADPOOL");

        // finally, execute the term command
        // and execute the system call
        m_mini.spawnProcess({"SHELL=sh", "term", "sh"});
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
        m_mini.taskQueueExecute();
    }
};


int main(int argc, char* argv[])
{
    //(void)argc;
    //(void)argv;
    return ImGuiApp::run<MyApplication>("SDL Window", 1920, 1080);
}

#include <imgui_impl_sdl2.cpp>
#include <imgui_impl_sdlrenderer2.cpp>

#include <src/imgui_demo.cpp>
#include <src/imgui_widgets.cpp>
#include <src/imgui_draw.cpp>
#include <src/imgui_tables.cpp>
#include <src/imgui.cpp>
#include <imgui_stdlib.cpp>

