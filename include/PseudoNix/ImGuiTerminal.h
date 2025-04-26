#ifndef EBASH_IMGUI_TERMINAL_H
#define EBASH_IMGUI_TERMINAL_H

#include "System.h"
#include "ImGuiConsoleWidget.h"
#include <imgui.h>

namespace PseudoNix
{

inline System::task_type terminalWindow_coro(System::e_type ctrl)
{

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

    auto & m_mini = *ctrl->system;

#if 0
    auto shell_stdin = System::make_stream();
    auto shell_stdout = System::make_stream();
    System::pid_type sh_pid = 0xFFFFFFFF;
#else
    std::vector<std::string> args(ctrl->args.begin()+1, ctrl->args.end());
    if(args.empty())
        args.push_back("sh");
    auto sh_pid = ctrl->executeSubProcess(System::parseArguments(args));
    // Grab the input and output streams for the shell
    // command
    auto [shell_stdin, shell_stdout] = m_mini.getIO(sh_pid);
#endif

    bool open = true;
    bool show_buttons = !(ctrl->env.count("NO_SHOW_BUTTONS") == 1);
    bool exit_if_subprocess_exits = !(ctrl->env.count("NO_AUTO_CLOSE") == 1);
    //bool exit_if_subprocess_exits = true;


    int frameCount[2] = {ImGui::GetFrameCount()-1, ImGui::GetFrameCount()-1};
    while(open)
    {
        frameCount[0] = frameCount[1];
        frameCount[1] = ImGui::GetFrameCount();
        // its possible we could execute the
        // coroutines multiple times per ImGui frame
        // We dont want to draw the wiget multiple times
        // so only do this if the frames are different
        if(frameCount[0] != frameCount[1])
        {
            //--------------------------------------------------------------
            // The ImGui Draw section. Do not co_await
            // between between ImGui::Begin/ImGui::End;
            //--------------------------------------------------------------
            ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_Once);
            ImGui::Begin(std::format("Terminal {}", terminal_name).c_str(), &open);

            if(show_buttons)
            {
                if( ImGui::Button("New Terminal") )
                {
                    m_mini.runRawCommand({ctrl->args});
                }
                ImGui::SameLine();
                if( ImGui::Button("Sig-Int (Ctrl+C)") )
                {
                    m_mini.signal(sh_pid, PseudoNix::sig_interrupt);
                }
                ImGui::SameLine();
                if( ImGui::Button("End Stream (Ctrl+D)") )
                {
                    shell_stdin->set_eof();
                }
                ImGui::SameLine();
                if( ImGui::Button("Sig-Term") )
                {
                    m_mini.signal(sh_pid, PseudoNix::sig_terminate);
                }
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
        }
        //--------------------------------------------------------------

        if(exit_if_subprocess_exits && !ctrl->system->isRunning(sh_pid)) break;

        auto returned_signal = co_await ctrl->await_yield();

        switch(returned_signal)
        {
            case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT:   ctrl->system->clearSignal(ctrl->get_pid()); break;
            case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}
                default: break;
        }

        char c;
        while(shell_stdout->get(&c) == ReaderWriterStream::Result::SUCCESS)
        {
            console.pushLogByte(c);
        }
    }

    if(ctrl->system->isRunning(sh_pid))
    {
        ctrl->system->kill(sh_pid);
        co_await ctrl->await_finished(sh_pid);
    }

    co_return 0;
}

}

#endif
