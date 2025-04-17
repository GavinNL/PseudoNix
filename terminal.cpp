#define IMGUI_DEFINE_MATH_OPERATORS
#include "App.h"

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/ImGuiTerminal.h>


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

        // The term function is specific to this application, it will open a new
        // ImGui window to show you a terminal emulator
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

        // finally, execute the term command
        // and execute the system call
        m_mini.spawnProcess({"SHELL=sh", "term", "sh"});
    }

    void imguiRender()
    {
        // Each time imgui performs the rendering
        // we are going to execute the scheduler and invoke
        // all coroutines once. Some coroutines can draw to the
        // screen
        m_mini.executeAllFor(std::chrono::milliseconds(1), 10);
    }

};

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
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

