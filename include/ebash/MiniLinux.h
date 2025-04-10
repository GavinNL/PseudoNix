#ifndef MINI_LINUX_H
#define MINI_LINUX_H

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <filesystem>
#include <future>

#include "ReaderWriterStream.h"
#include "task.h"

namespace bl
{

struct MiniLinux
{
    using stream_type = bl::ReaderWriterStream;

    using pid_type         = uint32_t;
    using return_code_type = int32_t;
    using path_type        = std::filesystem::path;
    using task_type        =  gul::Task_t<return_code_type, std::suspend_always, std::suspend_always>;


    struct ProcessControl
    {
        bool sig_kill = false;
        MiniLinux *mini = nullptr;
    };

    struct Exec
    {
        std::vector<std::string>           args;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::map<std::string, std::string> env;

        std::shared_ptr<ProcessControl> control;

        // If the process performs a lot of suspends
        // use this to determine when to quit
        //
        // while(!is_sigkill())
        // {
        //    // do work
        //    co_await std::suspend_always{};
        // }
        //
        bool is_sigkill() const
        {
            return control->sig_kill;
        }

        Exec& operator << (std::string_view const &ss)
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
            if(!out)
            {
                std::cout << d;
                std::flush(std::cout);
                return *this;
            }
            out->put(d);
            return *this;
        }
    };

    using e_type = Exec;
    using function_type    = std::function< task_type(e_type)>;

    void clearFunction(std::string name)
    {
        m_funcs.erase(name);
    }
    void setFunction(std::string name, std::function< task_type(e_type) > _f)
    {
        m_funcs[name] = _f;
    }


    static std::shared_ptr<stream_type> make_stream(std::string const& initial_data="")
    {
        auto r = std::make_shared<stream_type>();
        *r << initial_data;
        return r;
    }


    MiniLinux()
    {
        setDefaultFunctions();
    }


    /**
     * @brief runRawCommand
     * @param e_type
     * @return
     *
     * You should not need to use this function:
     *
     * This command searches for the appropriate command
     * in the command list, e_typeutes the coroutine and
     * returns the task.
     *
     * You must explicitally e_typeute the task yourself
     * in your own scheduler.
     *
     * This function ensures that the output stream
     * is closed when the command completes.
     *
     *   e_type e_type;
     *   e_type.args = {"echo", "hello", "world"};
     *   e_type.in  = std::make_shared<stream_type>();
     *   e_type.out = std::make_shared<stream_type>();
     *   e_type.in->close();
     *
     *   // This returns  co-routine task that must
     *   // be waited on
     *   auto shell_task = M.runRawCommand(e_type);
     *
     *   while(!shell_task.done())
     *   {
     *       shell_task.resume();
     *   }
     *   assert( shell_task() == 0);
     *
     *
     */
    pid_type runRawCommand2(e_type args)
    {
        // Try to find the name of the function to run
        auto it = m_funcs.find(args.args[0]);
        if(it ==  m_funcs.end())
            return 0xFFFFFFFF;

        uint32_t _pid = _pid_count++;

        auto & exec_args = args;

        if(!exec_args.control)
            exec_args.control = std::make_shared<ProcessControl>();

        exec_args.control->mini = this;

        if(!exec_args.in)
        {
            exec_args.in = make_stream();
            exec_args.in->close();
        }

        exec_args.env["PID"] = std::to_string(_pid);

        if(m_preExec)
            m_preExec(exec_args);

        // run the function, it is a coroutine:
        // it will return a task}
        auto T = it->second(exec_args);

        Process _t = { std::promise<int>(), std::move(exec_args), std::move(T)};
        m_procs2.emplace(_pid, std::move(_t));

        return _pid;
    }

    pid_type runRawCommand(e_type args)
    {
        return runRawCommand2(args);
    }
    /**
     * @brief getProcessFuture
     * @param pid
     * @return
     *
     * Return the std::future of the pid. This can only be
     * called once. Only use this if you need a future
     * for tracking purposes. The future will become available
     * when the processs has finished
     */
    std::future<return_code_type> getProcessFuture(uint32_t pid)
    {
        return m_procs2.at(pid).return_promise.get_future();
    }

    bool isRunning(pid_type pid) const
    {
        return m_procs2.count(pid) != 0;
    }
    void kill(pid_type pid) const
    {
        if(isRunning(pid))
        {
            m_procs2.at(pid).exec.control->sig_kill=true;
        }
    }
    /**
     * @brief execute
     * @param pid
     *
     * Executes a specific PID and returns true if the
     * coroutine is completed
     */
    bool execute(uint32_t pid)
    {
        auto & coro = m_procs2.at(pid);
        if(!coro.task.done())
        {
            coro.task.resume();
        }
        if(coro.task.done())
        {
            auto exit_code = coro.task();
            coro.exec.env["?"] = std::to_string(exit_code);

            if(m_postExec)
                m_postExec(coro.exec);

            coro.return_promise.set_value(exit_code);

            if(coro.exec.out)
            {
                coro.exec.out->close();
            }

            return true;
        }
        return false;
    }

    size_t executeAll()
    {
        for(auto it = m_procs2.begin(); it!=m_procs2.end();)
        {
            auto & pid = it->first;
            if(execute(pid))
            {
                it = m_procs2.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return m_procs2.size();
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


    //std::function< std::future<int>(task_type&&) > m_scheduler;
    std::function< void(e_type&) >                 m_preExec;
    std::function< void(e_type&) >                 m_postExec;

    struct Process
    {
        std::promise<int> return_promise;
        e_type            exec;
        task_type         task;
    };


    std::map<std::string, std::function< task_type(e_type) >> m_funcs;
protected:

    std::map<uint32_t, Process > m_procs2;
    uint32_t _pid_count=1;

    void setDefaultFunctions()
    {
        m_funcs["false"] = [](e_type args) -> task_type
        {
            (void)args;
            co_return 1;
        };
        m_funcs["true"] = [](e_type args) -> task_type
        {
            (void)args;
            co_return 0;
        };
        m_funcs["help"] = [this](e_type args) -> task_type
        {
            args << "List of commands:\n\n";
            for(auto & f : m_funcs)
            {
                args << f.first << '\n';
            }
            co_return 0;
        };
        m_funcs["env"] = [this](e_type args) -> task_type
        {
            for(auto & [var, val] : args.env)
            {
                args << std::format("{}={}\n", var,val);
            }
            co_return 0;
        };
        m_funcs["echo"] = [](e_type args) -> task_type
        {
            for(size_t i=1;i<args.args.size();i++)
            {
                args << args.args[i] << (i==args.args.size()-1 ? "\n" : " ");
            }
            co_return 0;
        };
        m_funcs["sleep"] = [](e_type args) -> task_type
        {
            std::string output;
            if(args.args.size() < 2)
                co_return 1;
            float t = 0.0f;
            std::istringstream in(args.args[1]);
            in >> t;

            auto T1 = std::chrono::system_clock::now() +
                        std::chrono::milliseconds( static_cast<uint64_t>(t*1000));
            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            while(std::chrono::system_clock::now() < T1)
            {
                co_await std::suspend_always{};
            }
            co_return 0;
        };
        m_funcs["rev"] = [](e_type args) -> task_type
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
                        args << output << '\n';
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
                args << output << '\n';
            }
            co_return 0;
        };
        m_funcs["wc"] = [](e_type args) -> task_type
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

            args << std::to_string(i) << '\n';
            //std::cout << std::to_string(i);
            co_return 0;
        };
        m_funcs["ps"] = [](e_type args) -> task_type
        {
            auto & M = *args.control->mini;

            args << std::format("PID   CMD\n");
            for(auto & [pid, P] : M.m_procs2)
            {
                std::string cmd;
                for(auto & c : P.exec.args)
                    cmd += c + " ";
                args << std::format("{}     {}\n", pid, cmd);
            }

            //std::cout << std::to_string(i);
            co_return 0;
        };
        m_funcs["kill"] = [](e_type args) -> task_type
        {
            if(args.args.size() < 2)
                co_return 1;

            pid_type pid = 0;
            auto [ptr, ec] = std::from_chars(args.args[1].data(), args.args[1].data() + args.args[1].size(), pid);
            (void)ec;
            (void)ptr;
            auto & M = *args.control->mini;
            auto it = M.m_procs2.find(pid);
            if(it != M.m_procs2.end())
            {
                it->second.exec.control->sig_kill = true;
                co_return 0;
            }
            args << std::format("Could not find process ID: {}\n", pid);
            co_return 1;
        };
    }

};


}


#endif
