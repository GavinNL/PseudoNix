#ifndef MINI_LINUX_H
#define MINI_LINUX_H

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <filesystem>

#include "ReaderWriterStream.h"
#include "task.h"

namespace bl
{


struct MiniLinux
{
    using stream_type = bl::ReaderWriterStream;
    using path_type   = std::filesystem::path;
    using task_type   = gul::Task_t<int>;

    struct Exec
    {
        std::vector<std::string>           args;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::map<std::string, std::string> env;
        std::filesystem::path              work_dir;

        Exec& operator << (std::string const &ss)
        {
            if(!out)
            {
                std::cout << ss;
                std::flush(std::cout);
                return *this;
            }
            for(auto i : ss)
            {
                out->put(i);
            }
            return *this;
        }
        Exec& operator << (char d)
        {
            out->put(d);
            return *this;
        }
    };

    std::map<std::string, std::function< task_type(Exec) >> funcs;


    static std::shared_ptr<stream_type> make_stream(std::string const& initial_data="")
    {
        auto r = std::make_shared<stream_type>();
        *r << initial_data;
        return r;
    }
    //=======================================================================
    // These are some functionsl you can use to
    // provide special behaviour with your system
    //=======================================================================
    std::function<bool(path_type)> path_exists = [](path_type) {
        return false;
    };
    //=======================================================================


    MiniLinux()
    {
        setDefaultFunctions();
    }

#define MINILINUX_PROCESS_TASK(T) {\
    while(!T.done())\
        {\
                co_await std::suspend_always{};\
                T.resume();\
        }\
}

/**
     * @brief runRawCommand
     * @param exec
     * @return
     *
     * This is the main function to call. This does not do any
     * fancy parsing command line data, you have to set each
     * argment separately as well as set the input and output
     * streams:
     *
     *   Exec exec;
     *   exec.args = {"echo", "hello", "world"};
     *   exec.in  = std::make_shared<stream_type>();
     *   exec.out = std::make_shared<stream_type>();
     *   exec.in->close();
     *
     *   // This returns  co-routine task that must
     *   // be waited on
     *   auto shell_task = M.runRawCommand(exec);
     *
     *   while(!shell_task.done())
     *   {
     *       shell_task.resume();
     *   }
     *   assert( shell_task() == 0);
     *
     *
     * A "sh" command has been provided that provides
     * all the commandline parsing for you, but you have
     * to run it slightly differently.
     *
     *   Exec sh;
     *   sh.args = {"sh"};
     *   sh.in  = std::make_shared<stream_type>();
     *   sh.out = std::make_shared<stream_type>();
     *
     *   // The regular Linux "sh" command actually reads
     *   // commands from standard-in, so you have to place your
     *   // shell script in the
     *   *sh.in << "echo hello ; sleep 2.0 ; echo world";
     *
     *   // We have to close the input stream otherwise
     *   // the "sh" command will keep waiting on more data
     *   //
     *   sh.in->close();
     *   auto shell_task = M.runRawCommand(sh);
     *   while(!shell_task.done())
     *   {
     *       shell_task.resume();
     *   }
     *   sh.out->toStream(std::cout);
     *
     */
    task_type runRawCommand(Exec exec)
    {
        auto & args = exec.args;

        auto it = funcs.find(args[0]);
        if(it ==  funcs.end())
            co_return 127;

        auto T = it->second(exec);

        while (!T.done()) {
            co_await std::suspend_always{};
            T.resume();
        }

        //MINILINUX_PROCESS_TASK(T)
        if(exec.out)
            exec.out->close();

        // Return the final value
        co_return T();
    }

    static std::vector<std::string> cmdLineToArgs(std::string_view line)
    {
        std::vector<std::string> _cmds(1);
        // convert line onto string views
        char currentQuote = 0;

        for(auto c : line)
        {
            if( currentQuote == 0)
            {
                if( c == '"')
                {
                    currentQuote = '"';
                    continue;
                }
                if( c == '\'')
                {
                    currentQuote = '\'';
                    continue;
                }
                if(c == ' ' && !_cmds.back().empty())
                {
                    _cmds.push_back({});
                }
                if(c != ' ')
                {
                    if( c == '|' )
                    {
                        _cmds.push_back({});
                        _cmds.back().push_back('|');
                        _cmds.push_back({});
                    }
                    else
                    {
                        _cmds.back().push_back(c);
                    }
                    //if(_cmds.back().size() && _cmds.back().back() != '|')
                    }
            }
            else
            {
                // we are currently in a quoted string
                if( c == currentQuote)
                {
                    currentQuote = 0;
                    continue;
                }
                else
                {
                    _cmds.back().push_back(c);
                }
            }
        }
        auto dn = std::remove_if( begin(_cmds), end(_cmds), [](auto & c) { return c.empty(); });
        _cmds.erase(dn, end(_cmds));
        return _cmds;
    }

protected:

    void setDefaultFunctions()
    {
        // By default set up some initial commands
        funcs["sh"] = std::bind(sh, std::placeholders::_1, this);

        funcs["false"] = [](Exec args) -> task_type
        {
            (void)args;
            co_return 1;
        };
        funcs["true"] = [](Exec args) -> task_type
        {
            (void)args;
            co_return 0;
        };
        funcs["echo"] = [](Exec args) -> task_type
        {
            (void)args;
            for(size_t i=1;i<args.args.size();i++)
            {
                args << args.args[i] << (i==args.args.size()-1 ? "\n" : " ");
            }
            co_return 0;
        };
        funcs["sleep"] = [](Exec args) -> task_type
        {
            std::string output;
            if(args.args.size() < 2)
                co_return 1;
            float t = 0.0f;
            std::istringstream in(args.args[1]);
            in >> t;
            auto T = std::chrono::milliseconds( static_cast<uint64_t>(t*1000));
            auto T0 = std::chrono::system_clock::now();
            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            while(std::chrono::system_clock::now()-T0 < T)
            {
                co_await std::suspend_always{};
            }
            co_return 0;
        };
        funcs["rev"] = [](Exec args) -> task_type
        {
            std::string output;

            while(true)
            {
                while(args.in->has_data())
                {
                    output.push_back(args.in->get());
                    if(output.back() == '\n')
                    {
                        output.pop_back();
                        std::reverse(output.begin(), output.end());
                        args << output;
                        output.clear();
                    }
                }
                if(args.in->eof())
                    break;
                else
                    co_await std::suspend_always{};
            }

            if(!output.empty())
            {
                std::reverse(output.begin(), output.end());
                args << output;
            }
            co_return 0;
        };
        funcs["wc"] = [](Exec args) -> task_type
        {
            uint32_t i=0;

            while(true)
            {
                while(args.in->has_data())
                {
                    args.in->get();
                    ++i;
                }
                if(args.in->eof())
                    break;
                else
                    co_await std::suspend_always{};
            }

            args << std::to_string(i);
            //std::cout << std::to_string(i);
            co_return 0;
        };
    }

    static task_type runPipedCommand(std::string cmdLine,
                                          std::map<std::string, std::string> env,
                                          std::shared_ptr<stream_type> in,
                                          std::shared_ptr<stream_type> out,
                                          MiniLinux * mininix)
    {
        std::vector<Exec> chain;

        std::string cmd;
        auto lastOut = in;
        for(auto c : cmdLine)
        {
            if(c != '|')
            {
                if(!(cmd.empty() && std::isspace(c)))
                    cmd += c;
            }
            else
            {
                auto & exec = chain.emplace_back();
                exec.args = cmdLineToArgs(cmd);
                exec.in = lastOut;
                exec.out = std::make_shared<stream_type>();
                exec.env = env;
                lastOut = exec.out;
                cmd.clear();
            }
        }
        if(!cmd.empty())
        {
            auto & exec = chain.emplace_back();
            exec.env = env;
            exec.args = cmdLineToArgs(cmd);
            exec.in = lastOut;
            exec.out = out;
        }
        chain.front().in = in;

        // place all the commands in a task
        // queue.
        std::vector<task_type> _tasks;
        for(auto & _C : chain)
        {
            _tasks.emplace_back(mininix->runRawCommand(_C));
        }

        while(true)
        {
            uint32_t count = 0;
            for(size_t i=0;i<chain.size();i++)
            {
                auto & T = _tasks[i];

                if(T.done())
                {
                    count++;
                    // we need to close the output stream
                    // so that the input for the next stage
                    // will return a proper eof()
                }
                else
                {
                    T.resume();
                }
            }
            if(count == chain.size())
                break;
            co_await std::suspend_always{};
        }

        co_return _tasks.back()();
    }

    // This is the final shell executor
    // workflow
    static auto sh(Exec args, MiniLinux* mininix) -> task_type
    {
        //  // Note that the sh command
        //  // actually reads data from the  input stream
        //  auto stdin = std::make_shared<stream_type>();
        //  *stdin << "echo hello ${HOME} $(echo hello world [${HOME}] ) | rev";

        //  // we're going to close the stream after writing the data
        //  // otherwise it will block on
        //  // waiting for more bytes
        //  stdin->close();
        auto & stdin = args.in;
        auto & stdout = args.out;

        auto originalEnv = args.env;

        args.env["PWD"] = args.work_dir;

        //std::string cmd = "echo hello ${HOME} $(echo hello world [${HOME}] ) | rev";
        while(true)
        {
            if(stdin->eof())
                break;

            if(!stdin->has_data())
                co_await std::suspend_always{};

            std::string cmd;
            bool runCommand=false;
            while(stdin->has_data())
            {
                cmd += stdin->get();
                if(cmd.back() == ';' || cmd.back() == '\n')
                {
                    cmd.pop_back();
                    runCommand=true;
                    break;
                }
            }
            if(stdin->eof())
                runCommand=true;

            if(!runCommand)
                continue;

            std::string stack;
            for(auto c : cmd)
            {
                if(c == '}')
                {
                    std::string newCmd;
                    // pop until you find {
                    while(stack.size())
                    {
                        newCmd += stack.back();
                        stack.pop_back();
                        if(newCmd.back() == '{')
                        {
                            assert(stack.back() == '$');
                            stack.pop_back();
                            newCmd.pop_back();
                            std::reverse(newCmd.begin(), newCmd.end());
                            stack += args.env[newCmd];
                            break;
                        }
                    }
                }
                else if(c == ')')
                {
                    std::string newCmd;
                    // pop until you find {
                    while(stack.size())
                    {
                        newCmd += stack.back();
                        stack.pop_back();
                        if(newCmd.back() == '(')
                        {
                            assert(stack.back() == '$');
                            stack.pop_back();
                            newCmd.pop_back();

                            std::reverse(newCmd.begin(), newCmd.end());

                            auto in = std::make_shared<stream_type>();
                            auto out = std::make_shared<stream_type>();
                            in->close();

                            auto chain = MiniLinux::cmdLineToChain(newCmd);
                            auto Task = executeNonPipedChain(chain, in, out, mininix, originalEnv, args.work_dir);

                            MINILINUX_PROCESS_TASK(Task)
                            args.env["?"] = std::to_string(Task());
                            out->close();
                            stack += out->str();
                            break;
                        }
                    }
                }
                else
                {
                    if(!(stack.empty() && std::isspace(c)))
                        stack.push_back(c);
                }
            }

            {
                auto cd_args = cmdLineToArgs(stack);
                if(cd_args.size())
                {
                    if(cd_args[0]=="cd")
                    {
                        auto newPath = (args.work_dir / cd_args[1]).lexically_normal();

                        if(mininix->path_exists(newPath))
                        {
                            args.env["OLDPWD"] = args.work_dir;
                            args.work_dir = newPath;
                            args.env["PWD"] = newPath.generic_string();
                        }
                        else
                        {
                            *args.out << "sh: cd: " << cd_args[1] << ": No such file or directory";
                        }
                        continue;
                    }
                    if(cd_args[0]=="pwd")
                    {
                        *args.out << args.work_dir.generic_string() << "\n";
                        continue;
                    }
                }
            }

            if(!stack.empty())
            {
                //auto in = std::make_shared<stream_type>();
                //auto out = std::make_shared<stream_type>();
                //in->close();
                auto chain = MiniLinux::cmdLineToChain(stack);
                auto Task = executeNonPipedChain(chain, stdin, stdout, mininix, originalEnv, args.work_dir);
                //auto Task = runPipedCommand(stack, args.env,stdin, stdout, mininix);
                MINILINUX_PROCESS_TASK(Task)
                args.env["?"] = std::to_string(Task());
                //out->close();
                //stack += out->str();
                //std::cout << fmt::format("[{}] {}\n", Task(), out->str()) << std::endl;
            }
        }
        co_return 0;
    };

public:
    struct CmdExecList
    {
        std::string cmdLine;
        enum
        {
            RUN_NEXT_ALWAYS,
            RUN_NEXT_ON_SUCCESS,
            RUN_NEXT_ON_FAIL,
            RUN_NEXT_WITH_PIPED,
        } next = RUN_NEXT_ALWAYS;
    };

    static
    task_type executeNonPipedChain(std::vector<CmdExecList> list,
                                        std::shared_ptr<MiniLinux::stream_type> in,
                                        std::shared_ptr<MiniLinux::stream_type> out,
                                        MiniLinux * mininix,
                                        std::map<std::string, std::string> env = {},
                                        path_type workDir = "/")
    {

        // run through the exec list and combined
        // any consecutive
        for(size_t i=0;i<list.size()-1;)
        {
            if(list[i].next == CmdExecList::RUN_NEXT_WITH_PIPED)
            {
                list[i].cmdLine += " | ";
                list[i].cmdLine += list[i+1].cmdLine;
                list[i].next = list[i+1].next;
                list.erase( std::next(list.begin(), static_cast<int32_t>(i+1)) );
            }
            else
            {
                ++i;
            }
        }

        int retcode = 0;
        for(auto &C : list)
        {
            //MiniLinux::Exec E;

            //E.args = MiniLinux::cmdLineToArgs(C.cmdLine);
            //E.in  = in;
            //E.out = out;

            auto chain = cmdLineToChain(C.cmdLine);
            auto task = executePipedChain(chain,in, out, mininix, env, workDir);

            retcode = task();
            while(!task.done())
            {
                co_await std::suspend_always{};
                task.resume();
            }
            if(C.next == CmdExecList::RUN_NEXT_ALWAYS)
            {
                continue;
            }
            else if(C.next == CmdExecList::RUN_NEXT_ON_SUCCESS)
            {
                if(retcode != 0)
                    break;
            }
            else if(C.next == CmdExecList::RUN_NEXT_ON_FAIL)
            {
                if(retcode == 0)
                    break;
            }
        }

        co_return retcode;
    }

    /**
 * @brief executePipedChain
 * @param list
 * @return
 *
 * all commands in the list must be piped into each other
 * and there must be NO shell substitutions ${} or $()
 *
 * cmd1 | cmd2 | cmd3
 *
 * NOT: cmd1 && cmd2
 */
    static
    task_type executePipedChain(std::vector<CmdExecList> list,
                                     std::shared_ptr<MiniLinux::stream_type> in,
                                     std::shared_ptr<MiniLinux::stream_type> out,
                                     MiniLinux * mininix,
                                     std::map<std::string, std::string> env = {},
                                     path_type work_dir = "/")
    {
        for(size_t i=0;i<list.size()-1;i++)
        {
            assert(list[i].next == CmdExecList::RUN_NEXT_WITH_PIPED);
        }

        std::vector<task_type > taskList;
        std::vector<MiniLinux::Exec> execList;
        int retcode = 0;

        for(auto &C : list)
        {
            auto & E = execList.emplace_back();
            E.args = MiniLinux::cmdLineToArgs(C.cmdLine);
            E.in = in;
            E.out = out;
            E.env = env;
            E.work_dir = work_dir;
        }

        if(list.size() > 1)
        {
            for(size_t i=0;i<execList.size()-1;i++)
            {
                execList[i].out  = std::make_shared<MiniLinux::stream_type>();
                execList[i+1].in = execList[i].out;
            }
        }

        for(auto & _C : execList)
        {
            taskList.emplace_back(mininix->runRawCommand(_C));
        }

        while(true)
        {
            uint32_t count = 0;
            for(size_t i=0;i<execList.size();i++)
            {
                auto & T = taskList[i];

                if(T.done())
                {
                    retcode = T();
                    count++;
                    // we need to close the output stream
                    // so that the input for the next stage
                    // will return a proper eof()
                }
                else
                {
                    T.resume();
                }
            }
            if(count == taskList.size())
                break;
            co_await std::suspend_always{};
        }
        co_return retcode;
    }


    /**
 * @brief cmdLineToChain
 * @param cmdLine
 * @return
 *
 * Returns a list of command lines separated by the following:
 *
 * && , || , | , ;
 *
 *
 */
    static
    std::vector<CmdExecList> cmdLineToChain(std::string_view cmdLine)
    {
        std::string cmd;
        std::vector<CmdExecList> chain;
        bool inQuotes = false;
#define PushIfNotSpace(str, _char) if(!(str.empty() && std::isspace(_char))) str.push_back(_char)
        for(auto c : cmdLine)
        {
            if(c == '\"')
            {
                if(inQuotes)
                {
                    inQuotes = false;
                }
                else
                {
                    inQuotes = true;
                }
            }
            if(cmd.size())
            {
                if(inQuotes)
                {
                    PushIfNotSpace(cmd, c);
                }
                else
                {
                    if(c == '&' && cmd.back() == '&')
                    {
                        auto & E = chain.emplace_back();
                        cmd.pop_back();
                        E.cmdLine = cmd;
                        //E.continueIfFailed = false;
                        E.next = CmdExecList::RUN_NEXT_ON_SUCCESS;
                        cmd.clear();
                    }
                    else if(c == '|' && cmd.back() == '|')
                    {
                        auto & E = chain.emplace_back();
                        cmd.pop_back();
                        E.cmdLine = cmd;
                        //E.continueIfFailed = true;
                        E.next = CmdExecList::RUN_NEXT_ON_FAIL;
                        cmd.clear();
                    }
                    else if(c != '|' && cmd.back() == '|')
                    {
                        auto & E = chain.emplace_back();
                        cmd.pop_back();
                        E.cmdLine = cmd;
                        E.next = CmdExecList::RUN_NEXT_WITH_PIPED;
                        cmd.clear();
                    }
                    else if(c == ';')
                    {
                        auto & E = chain.emplace_back();
                        //cmd.pop_back();
                        E.cmdLine = cmd;
                        E.next = CmdExecList::RUN_NEXT_ALWAYS;
                        cmd.clear();
                    }
                    else
                    {
                        PushIfNotSpace(cmd,c);
                    }
                }
            }
            else
            {
                PushIfNotSpace(cmd,c);
            }
        }

        if(cmd.size())
        {
            auto & E = chain.emplace_back();
            E.cmdLine = cmd;
            //E.continueIfFailed = false;
        }
        for(auto & c : chain)
        {
            while(std::isspace(c.cmdLine.back()))
                c.cmdLine.pop_back();
        }
#undef PushIfNotSpace
        return chain;
    }





};


}


#endif
