#ifndef EBASH_IMGUI_TERMINAL_H
#define EBASH_IMGUI_TERMINAL_H

#include "MiniLinux.h"
#include "ImGuiConsoleWidget.h"
#include <imgui.h>

namespace bl
{

bl::MiniLinux::task_type terminalWindow_coro(bl::MiniLinux::e_type ctrl)
{
    // we're going to suspend on first run
    // because we are calling ImGui::Begin/End
    // We need to make sure these are only called
    // within the ImGui context otherwise
    co_await std::suspend_always{};

    static int term_count=1;
    std::string terminal_name = std::format("Terminal {}", term_count++);

    ConsoleWindow console;

    console.AddLog("# Try the following commands:");
    console.AddLog("# ");
    console.AddLog("# help");
    console.AddLog("# echo hello ${USER}");
    console.AddLog("# env");
    console.AddLog("# env | rev");
    console.AddLog("# true && echo");
    console.AddLog("# ps");

    // This is going to be an ImGui process and we are
    // going to get command line inputs from the ImGui::TextEdit
    // widget. So we are going to create a subprocess for "sh"
    // with its own input and output streams
    //
    // Then we're going to get the input from the ImGui widget
    // and place it into the stream manually
    std::string _cmdline;
    std::string output;

    auto & m_mini = *ctrl->mini;

    std::vector<std::string> args(ctrl->args.begin()+1, ctrl->args.end());
    if(args.empty())
        args.push_back("sh");


    auto sh_pid = ctrl->executeSubProcess(bl::MiniLinux::parseArguments(args));

    // Grab the input and output streams for the shell
    // command
    auto [shell_stdin, shell_stdout] = m_mini.getIO(sh_pid);
    bl_defer
    {
        //std::cout << "destructor" << std::endl;
        shell_stdin->close();
    };
    //assert(shell_stdin == E.in);
    //assert(shell_stdout == E.out);

    bool exit_if_subprocess_exits = false;

    while(true)
    {
        //--------------------------------------------------------------
        // The ImGui Draw section. Do not co_await
        // between between ImGui::Begin/ImGui::End;
        //--------------------------------------------------------------
        ImGui::Begin(std::format("Terminal {}", terminal_name).c_str());

        if( ImGui::Button("New Terminal") )
        {
            m_mini.runRawCommand({ctrl->args});
        }
        ImGui::SameLine();
        if( ImGui::Button("Sig-Int") )
        {
            m_mini.signal(sh_pid, SIGINT);
        }
        ImGui::SameLine();
        if( ImGui::Button("Sig-Term") )
        {
            m_mini.signal(sh_pid, SIGTERM);
        }
        // Draw the console and invoke the callback if
        // a command is entered into the input text
        console.Draw([_in=shell_stdin](std::string const & cmd)
                     {
                         // place all the bytes in the cmd string into the
                         // input stream so that sh can process it.
                         *_in << cmd;
                         *_in << '\n'; // this is required for the shell to execute the command
                     });

        ImGui::End();
        //--------------------------------------------------------------

        if(exit_if_subprocess_exits && ctrl->areSubProcessesFinished()) break;

        // Suspend
        SUSPEND_SIG_TERM(ctrl)

        while(shell_stdout->has_data())
        {
            auto c = shell_stdout->get();
            if(c == '\n')
            {
                console.AddLog(output);
                output.clear();
            } else {
                output+= c;
            }
        }
    }


    co_return 0;
}

}

#endif
