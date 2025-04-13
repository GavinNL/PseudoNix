#ifndef EBASH_IMGUI_TERMINAL_H
#define EBASH_IMGUI_TERMINAL_H

#include "MiniLinux.h"
#include "ImGuiConsoleWidget.h"
#include <imgui.h>

namespace bl
{

bl::MiniLinux::task_type terminalWindow_coro(bl::MiniLinux::e_type control)
{
    auto & E = *control;
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

    auto & m_mini = *control->mini;

    std::vector<std::string> args(control->args.begin()+1, control->args.end());
    if(args.empty()) args.push_back("sh");
    // Execute the system call for the shell funciton
    auto sh_pid = m_mini.runRawCommand({ args, E.env});

    // Grab the input and output streams for the shell
    // command
    auto [shell_stdin, shell_stdout] = m_mini.getIO(sh_pid);
    bl_defer
    {
        //std::cout << "destructor" << std::endl;
        shell_stdin->close();
    };

    bool alreadyHandled = false;
    control->setSignalHandler([&](int s)
                              {
                                  //std::cout << std::format("Term signal. Killing {}", sh_pid) << std::endl;
                                  if(!alreadyHandled)
                                  {
                                      alreadyHandled = true;
                                      m_mini.signal(sh_pid, 2);
                                      alreadyHandled = false;
                                  }
                              });

    try {
        while(true)
        {
            // Did we get a kill signal to kill the process
            // safely?
            if(E.is_sigkill())
            {
                break;
            }

            ImGui::Begin(std::format("Terminal {}", terminal_name).c_str());

            if( ImGui::Button("New Terminal") )
            {
                m_mini.runRawCommand({control->args});
            }
            ImGui::SameLine();
            if( ImGui::Button("Sig-Term") )
            {
                m_mini.signal(sh_pid, 2);
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

            // check if the future (return value of sh) has been set
            // if it has, that means the shell process exited
            // so we can close this window
            if(E.mini->isRunning(sh_pid))
            {
                co_await std::suspend_always{};
                // Grab any characters from the output stream and
                // add it to the console log

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
            else
            {
                break;
            }
        }
    }
    catch ( std::exception & e)
    {
        console.AddLog(e.what());
    }

    co_return 0;
}

}

#endif
