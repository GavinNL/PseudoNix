#ifndef EBASH_IMGUI_TERMINAL_H
#define EBASH_IMGUI_TERMINAL_H

#include "System.h"
#include "ImGuiConsoleWidget.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace PseudoNix
{

inline System::task_type terminalWindow_coro(System::e_type ctrl)
{
    PN_PROC_START(ctrl);

    ConsoleWindow console;

    // This is going to be an ImGui process and we are
    // going to get command line inputs from the ImGui::TextEdit
    // widget. So we are going to create a subprocess for "sh"
    // with its own input and output streams
    //
    // Then we're going to get the input from the ImGui widget
    // and place it into the stream manually
    std::string _cmdline;

    std::vector<std::string> args(ARGS.begin() + 1, ARGS.end());
    if(args.empty())
        args.push_back("sh");
    auto sh_pid = ctrl->executeSubProcess(System::parseArguments(args));
    // Grab the input and output streams for the shell
    // command
    auto [shell_stdin, shell_stdout] = SYSTEM.getIO(sh_pid);

    bool open = true;
    bool show_buttons = !(ENV.count("NO_SHOW_BUTTONS") == 1);
    bool exit_if_subprocess_exits = !(ENV.count("NO_AUTO_CLOSE") == 1);

    if(SYSTEM.taskQueueExists("IMGUI"))
    {
        PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield("IMGUI"), ctrl);
    }

    ENV["TERMINAL_NAME"] = ENV["TERMINAL_NAME"].empty() ? "Terminal" : ENV["TERMINAL_NAME"];
    int frameCount[2] = {ImGui::GetFrameCount()-1, ImGui::GetFrameCount()-1};
    while(open)
    {
        auto g = ImGui::GetCurrentContext();

        frameCount[0] = frameCount[1];
        frameCount[1] = ImGui::GetFrameCount();
        // its possible we could execute the
        // coroutines multiple times per ImGui frame
        // We dont want to draw the wiget multiple times
        // so only do this if the frames are different
        if(g->WithinFrameScope && frameCount[0] != frameCount[1])
        {
            //--------------------------------------------------------------
            // The ImGui Draw section. Do not co_await
            // between between ImGui::Begin/ImGui::End;
            //--------------------------------------------------------------
            ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_Once);
            ImGui::Begin(std::format("{}##{}", ENV["TERMINAL_NAME"], PID).c_str(), &open);

            if(show_buttons)
            {
                if( ImGui::Button("New Terminal") )
                {
                    ctrl->executeSubProcess(ARGS);
                }
                ImGui::SameLine();
                if( ImGui::Button("Sig-Int (Ctrl+C)") )
                {
                    SYSTEM.signal(sh_pid, PseudoNix::eSignal::INTERRUPT);
                }
                ImGui::SameLine();
                if( ImGui::Button("End Stream (Ctrl+D)") )
                {
                    shell_stdin->set_eof();
                }
                ImGui::SameLine();
                if( ImGui::Button("Sig-Term") )
                {
                    SYSTEM.signal(sh_pid, PseudoNix::eSignal::TERMINATE);
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

        if(exit_if_subprocess_exits && !SYSTEM.isRunning(sh_pid)) break;

        auto returned_signal = co_await ctrl->await_yield(QUEUE);

        switch(returned_signal)
        {
            case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT:   SYSTEM.clearSignal(PID); break;
            case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}
                default: break;
        }

        char c;
        while(shell_stdout->get(&c) == ReaderWriterStream::Result::SUCCESS)
        {
            console.pushLogByte(c);
        }
    }

    if(SYSTEM.isRunning(sh_pid))
    {
        SYSTEM.kill(sh_pid);
        PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_finished(sh_pid), ctrl);
    }

    co_return 0;
}

inline System::task_type imguiDemo_coro(System::e_type ctrl)
{
    PN_PROC_START(ctrl);

    if (SYSTEM.taskQueueExists("IMGUI"))
    {
        PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield("IMGUI"), ctrl);
    }

    int frameCount[2] = {ImGui::GetFrameCount() - 1, ImGui::GetFrameCount() - 1};

    bool open = true;
    while (true)
    {
        auto g = ImGui::GetCurrentContext();

        frameCount[0] = frameCount[1];
        frameCount[1] = ImGui::GetFrameCount();
        // its possible we could execute the
        // coroutines multiple times per ImGui frame
        // We dont want to draw the wiget multiple times
        // so only do this if the frames are different
        if (g->WithinFrameScope && frameCount[0] != frameCount[1])
        {
            ImGui::ShowDemoWindow(&open);
        }
        if (!open)
            break;
        PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
    }

    co_return 0;
}

inline System::task_type processMonitor_coro(System::e_type ctrl)
{
    PN_PROC_START(ctrl);

    if (SYSTEM.taskQueueExists("IMGUI"))
    {
        PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield("IMGUI"), ctrl);
    }

    int frameCount[2] = {ImGui::GetFrameCount() - 1, ImGui::GetFrameCount() - 1};

    std::string windowName = std::format("{}##{}", "Process Monitor", PID);
    bool open = true;
    while (true)
    {
        auto g = ImGui::GetCurrentContext();

        frameCount[0] = frameCount[1];
        frameCount[1] = ImGui::GetFrameCount();
        // its possible we could execute the
        // coroutines multiple times per ImGui frame
        // We dont want to draw the wiget multiple times
        // so only do this if the frames are different
        if (g->WithinFrameScope && frameCount[0] != frameCount[1])
        {
            //--------------------------------------------------------------
            // The ImGui Draw section. Do not co_await
            // between between ImGui::Begin/ImGui::End;
            //--------------------------------------------------------------
            ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_Once);

            ImGui::Begin(windowName.c_str(), &open);

            ImGui::Text("Work-In-Progress Process Monitor");

            ImGui::Separator();
            if (ImGui::BeginTable("table1", 3))
            {
                for (auto p : SYSTEM.get_processes())
                {
                    ImGui::TableNextRow();
                    ImGui::PushID(p);
                    auto &P = SYSTEM.PROC_AT(p);

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d %s", PID, P->args[0].c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::BeginDisabled(SYSTEM.getProcessUser(p) != U_ID);

                    auto state = SYSTEM.PROC_AT(p)->state;
                    if (ImGui::Button("Kill"))
                    {
                        SYSTEM.kill(p);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(state == System::Process::SUSPENDED ? "Resume" : "Pause")) {
                        SYSTEM.signal(p,
                                      state == System::Process::SUSPENDED ? eSignal::CONTINUE
                                                                          : eSignal::STOP);
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
            ImGui::End();
        }
        if (!open)
            break;
        PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
    }

    co_return 0;
}

inline void enable_default_imgui(System &sys)
{
    sys.setFunction("term", "Terminal emulator", PseudoNix::terminalWindow_coro);
    sys.setFunction("processMonitor", "Process Monitor", PseudoNix::processMonitor_coro);
    sys.setFunction("imgui_demo", "ImGui Demo Window", PseudoNix::imguiDemo_coro);
}
}

#endif
