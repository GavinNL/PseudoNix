#ifndef IMGUI_EXTRA_CONSOLE_WINDOW_H
#define IMGUI_EXTRA_CONSOLE_WINDOW_H

#include <imgui.h>
#include <imgui_stdlib.h>
#include <vector>
#include <format>

/**
 * @brief The ConsoleWindow class
 *
 * This ConsoleWindow was taken from the imgui_demo.cpp file and modified
 */
struct ConsoleWindow
{
    static inline bool _show = false;
    static inline const char * _desc = "Command Console";

    std::string              InputBuff;
    std::vector<std::string> m_logItems;
    std::vector<std::string> Commands;
    std::vector<std::string> History;
    int                      HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
    ImGuiTextFilter          Filter;
    bool                     AutoScroll = true;
    bool                     ScrollToBottom;


    bool m_showOutput = false;
    bool m_reclaim_focus = true;
    bool m_init = false;

    ConsoleWindow()
    {
        //IMGUI_DEMO_MARKER("Examples/Console");
        ClearLog();
        //memset(InputBuf, 0, sizeof(InputBuf));
        HistoryPos = -1;

        // "CLASSIFY" is here to provide the test case where "C"+[tab] completes to "CL" and display multiple matches.
        //Commands.push_back("HELP");
        Commands.push_back("history");
        Commands.push_back("clear");
        //Commands.push_back("CLASSIFY");

        AutoScroll = true;
        ScrollToBottom = false;
        //AddLog("Welcome to Dear ImGui!");
    }


    ~ConsoleWindow()
    {
        ClearLog();
    }

    void ClearLog()
    {
        m_logItems.clear();
    }

    void AddLog(std::string const & v)
    {
        m_logItems.push_back(v);
        if(m_logItems.back().back() == '\n')
        {
            m_logItems.back().pop_back();
            m_logItems.emplace_back();
        }
    }

    void pushLogByte(char c)
    {
        if(c=='\n')
        {
            m_logItems.push_back({});
        }
        else
        {
            if(m_logItems.empty())
                m_logItems.push_back({});
            m_logItems.back().push_back(c);
        }
    }


    template<typename executor_t>
    void Draw(executor_t && _executor)
    {
        m_showOutput = ImGui::GetWindowHeight() > 100;

        // As a specific feature guaranteed by the library, after calling Begin() the last Item represent the title bar.
        // So e.g. IsItemHovered() will return true when hovering the title bar.
        // Here we create a context menu only available from the title bar.
        if (ImGui::BeginPopupContextItem("Console Popup"))
        {
            if (ImGui::MenuItem("Close Console"))
            {
             //   *p_open = false;
            }
            ImGui::EndPopup();
        }

#if 0
     ImGui::TextWrapped(
         "This example implements a console with basic coloring, completion (TAB key) and history (Up/Down keys). A more elaborate "
         "implementation may want to store entries along with extra data such as timestamp, emitter, etc.");
     ImGui::TextWrapped("Enter 'HELP' for help.");

        // TODO: display items starting from the bottom
        if (ImGui::SmallButton("Add Debug Text"))  { AddLog( std::format("{} some text", 4)); AddLog("some more text"); AddLog("display very important message here!"); }
        ImGui::SameLine();
        if (ImGui::SmallButton("Add Debug Error")) { AddLog("[error] something went wrong"); }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear"))           { ClearLog(); }
        ImGui::SameLine();
        bool copy_to_clipboard = ImGui::SmallButton("Copy");
        //static float t = 0.0f; if (ImGui::GetTime() - t > 0.02f) { t = ImGui::GetTime(); AddLog("Spam %f", t); }

        ImGui::Separator();

        // Options menu
        if(ImGui::BeginPopup("Options"))
        {
            ImGui::Checkbox("Auto-scroll", &AutoScroll);
            ImGui::EndPopup();
        }

        // Options, Filter
        if (ImGui::Button("Options"))
            ImGui::OpenPopup("Options");
        ImGui::SameLine();
        Filter.Draw("Filter (\"incl,-excl\") (\"error\")", 180);
        ImGui::Separator();
#endif

        // Reserve enough left-over height for 1 separator + 1 input text
        const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

        if(m_showOutput)
        {
            if(ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar))
            {
                if(ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::Selectable("Clear"))
                        ClearLog();
                    ImGui::EndPopup();
                }

                // Display every line as a separate entry so we can change their color or add custom widgets.
                // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
                // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
                // to only process visible items. The clipper will automatically measure the height of your first item and then
                // "seek" to display only items in the visible area.
                // To use the clipper we can replace your standard loop:
                //      for (int i = 0; i < Items.Size; i++)
                //   With:
                //      ImGuiListClipper clipper;
                //      clipper.Begin(Items.Size);
                //      while (clipper.Step())
                //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                // - That your items are evenly spaced (same height)
                // - That you have cheap random access to your elements (you can access them given their index,
                //   without processing all the ones before)
                // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
                // We would need random-access on the post-filtered list.
                // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
                // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
                // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
                // to improve this example code!
                // If your items are of variable height:
                // - Split them into same height items would be simpler and facilitate random-seeking into your list.
                // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing

                //if (copy_to_clipboard)
                //    ImGui::LogToClipboard();

                for(auto & item : m_logItems)
                {
                    if (!Filter.PassFilter(item.data()))
                        continue;

                    // Normally you would store more information in your item than just a string.
                    // (e.g. make Items[] an array of structure, store color/type etc.)
                    ImVec4 color;
                    bool has_color = false;
                    if (strstr(item.data(), "[error]"))
                    {
                        color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true;
                    }
                    else if (strncmp(item.data(), "# ", 2) == 0)
                    {
                        color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true;
                    }
                    if (has_color)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                    }

                    ImGui::TextUnformatted(item.c_str());
                    if (has_color)
                    {
                        ImGui::PopStyleColor();
                    }
                }

                //if (copy_to_clipboard)
                //    ImGui::LogFinish();

                // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
                // Using a scrollbar or mouse-wheel will take away from the bottom edge.
                if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
                    ImGui::SetScrollHereY(1.0f);
                ScrollToBottom = false;

                ImGui::PopStyleVar();
            }

            ImGui::EndChild();
            ImGui::Separator();
        }

        // Command-line

        ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
        ImGui::PushItemWidth(-1);

        if (ImGui::InputText("##CMDInput", &InputBuff, input_text_flags, &TextEditCallbackStub, static_cast<void*>(this)) )
        {
            if(InputBuff.size())
            {
                ExecCommand(InputBuff, _executor);
            }
            InputBuff.clear();
            m_reclaim_focus = true;
        }
        ImGui::PopItemWidth();

        auto escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);

        if(!ImGui::IsItemFocused())
        {
            if(escapePressed)
            {
                _show = false;
            }
        }
        if(ImGui::IsItemDeactivatedAfterEdit())
        {
            if(InputBuff.empty() && escapePressed)
            {
                _show = false;
            }
        }
        else if(ImGui::IsItemDeactivated())
        {
            if(escapePressed)
            {
                _show = false;
            }
        }
        if(!ImGui::IsWindowFocused())
        {
            if(escapePressed)
            {
                _show = false;
            }
            if( ImGui::IsKeyPressed(ImGuiKey_GraveAccent))
            {
                m_reclaim_focus = true;
            }
        }
        // Auto-focus on window apparition
        ImGui::SetItemDefaultFocus();
        if (m_reclaim_focus)
        {
            ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget
            m_reclaim_focus = false;
        }

        //ImGui::End();
    }



    template<typename CommandExecutorCallable_t>
    void ExecCommand(std::string command_line, CommandExecutorCallable_t && executor)
    {
        AddLog(std::format("# {}\n", command_line));

        // Insert into history. First find match and delete it so it can be pushed to the back.
        // This isn't trying to be smart or optimal.
        HistoryPos = -1;
        History.push_back(command_line);

        // Process command
        if (command_line == "clear")
        {
            ClearLog();
        }
        //else if (command_line == "HELP")
        //{
        //    AddLog("Commands:");
        //    for(auto & c : Commands)
        //    {
        //        AddLog(std::format("- {}", c));
        //    }
        //}
        else if (command_line == "history")
        {
            int first = static_cast<int>(History.size()) - 10;
            for (int i = first > 0 ? first : 0; i < static_cast<int>(History.size()); i++)
            {
                AddLog( std::format("{}: {}\n", i, History[static_cast<size_t>(i)]));
            }
        }
        else
        {
            executor(command_line);
            // The drawing of this UI is called on the main thread
            // so we can execute action->runCommand as is. It will block
            // until its complete
            // if(127 ==  )
            // {
            //     AddLog( std::format("Unknown command: '{}'\n", command_line) );
            // }
            // else
            // {
            //     AddLog(_out.str());
            // }
        }

        // On command input, we scroll to bottom even if AutoScroll==false
        ScrollToBottom = true;
    }


    // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
    {
        ConsoleWindow* console = static_cast<ConsoleWindow*>(data->UserData);
        return console->TextEditCallback(data);
    }


    int TextEditCallback(ImGuiInputTextCallbackData* data)
    {
        //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
        switch (data->EventFlag)
        {
        case ImGuiInputTextFlags_CallbackCompletion:
        {
            // Example of TEXT COMPLETION

            // Locate beginning of current word
            const char* word_end = data->Buf + data->CursorPos;
            const char* word_start = word_end;
            while (word_start > data->Buf)
            {
                const char c = word_start[-1];
                if (c == ' ' || c == '\t' || c == ',' || c == ';')
                    break;
                word_start--;
            }

            // Build a list of candidates
            std::vector<const char*> candidates;
            for(auto & c : Commands)
            {
                auto sp = std::string_view(word_start, static_cast<size_t>(word_end - word_start));
                auto sp2 = std::string_view(&c[0], static_cast<size_t>(word_end - word_start));

                if( sp == sp2)
                {
                    candidates.push_back(c.data());
                }
            }

            if (candidates.size() == 0)
            {
                // No match
                AddLog( std::format("No match for \"{}\"!\n", word_start) );
            }
            else if (candidates.size() == 1)
            {
                // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
                data->DeleteChars( static_cast<int>(word_start - data->Buf), static_cast<int>(word_end - word_start));
                data->InsertChars(data->CursorPos, candidates[0]);
                data->InsertChars(data->CursorPos, " ");
            }
            else
            {
                // Multiple matches. Complete as much as we can..
                // So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
                int match_len = static_cast<int>(word_end - word_start);
                for (;;)
                {
                    int c = 0;
                    bool all_candidates_matches = true;
                    for (size_t i = 0; i < candidates.size() && all_candidates_matches; i++)
                    {
                        if (i == 0)
                            c = toupper(candidates[i][match_len]);
                        else if (c == 0 || c != toupper(candidates[i][match_len]))
                            all_candidates_matches = false;
                    }
                    if (!all_candidates_matches)
                        break;
                    match_len++;
                }

                if (match_len > 0)
                {
                    data->DeleteChars( static_cast<int>(word_start - data->Buf), static_cast<int>(word_end - word_start));
                    data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
                }

                // List matches
                AddLog("Possible matches:\n");
                for(size_t i = 0; i < candidates.size(); i++)
                    AddLog( std::format("- {}\n", candidates[i]) );
            }

            break;
        }
        case ImGuiInputTextFlags_CallbackHistory:
        {
            // Example of HISTORY
            const int prev_history_pos = HistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow)
            {
                if (HistoryPos == -1)
                    HistoryPos = static_cast<int>(History.size()) - 1;
                else if (HistoryPos > 0)
                    HistoryPos--;
            }
            else if (data->EventKey == ImGuiKey_DownArrow)
            {
                if (HistoryPos != -1)
                    if (++HistoryPos >= static_cast<int>(History.size()))
                        HistoryPos = -1;
            }

            // A better implementation would preserve the data on the current input line along with cursor position.
            if (prev_history_pos != HistoryPos)
            {
                const char* history_str = (HistoryPos >= 0) ? History[static_cast<size_t>(HistoryPos)].data() : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, history_str);
            }
        }
        }
        return 0;
    }
};

#endif
