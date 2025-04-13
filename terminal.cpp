#define IMGUI_DEFINE_MATH_OPERATORS
#include "App.h"
#include <iostream>

#include <ebash/MiniLinux.h>
#include <ebash/shell2.h>
#include <ebash/ImGuiTerminal.h>

#include <format>

struct MyApplication : public ImGuiApplication
{
public:
    bl::MiniLinux       m_mini;

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
        bl::ShellEnv shellEnv;
        m_mini.setFunction("sh", std::bind(bl::shell2, std::placeholders::_1, shellEnv));

        // The term function is specific to this application, it will open a new
        // ImGui window to show you a terminal emulator
        m_mini.setFunction("term", bl::terminalWindow_coro);

        m_mini.setFunction("theme", [](bl::MiniLinux::e_type control) -> bl::MiniLinux::task_type
        {
            auto & E = *control;
            if(E.args.size() > 1)
            {
                if(E.args[1] == "classic")
                {
                    ImGui::StyleColorsClassic();
                }
                if(E.args[1] == "light")
                {
                    ImGui::StyleColorsLight();
                }
                if(E.args[1] == "dark")
                {
                    ImGui::StyleColorsDark();
                }
            }
            else
            {
                E << "theme [classic|dark|light]\n";
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
        m_mini.m_preExec = [](bl::MiniLinux::Exec & E)
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
        // in our minilinux
        setupFunctions();

        // finally, execute the term command
        // and execute the system call
        bl::MiniLinux::Exec E;
        E.args = {"term", "sh"};
        E.env = {
            {"SHELL", "sh"}
        };

        m_mini.runRawCommand(E);
    }

    void imguiRender()
    {
        // Each time imgui performs the rendering
        // we are going to execute the scheduler and invoke
        // all coroutines once. Some coroutines can draw to the
        // screen
        m_mini.executeAllFor(std::chrono::milliseconds(1), 10);
        //ImGui::ShowDemoWindow(nullptr);
    }

};

int main(int, char**)
{
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

