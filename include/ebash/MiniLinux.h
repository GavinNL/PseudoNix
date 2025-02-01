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

struct ProcessControl
{
    bool sig_kill = false;
};

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

        // Set automatically
        MiniLinux *mini = nullptr;

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

    std::function< std::future<int>(task_type&&, Exec) > m_scheduler;
    std::map<std::string, std::function< task_type(Exec) >> funcs;


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
     * @brief system
     * @param exec
     * @return
     *
     * Execute a system call to the mini linux and place
     * the new process into the scheduler. Returns
     * a future<int> for the return code
     *
     * This is the main function you shoudl run
     * to execute something within the minilinux
     */
    std::future<int> system(Exec exec)
    {
        if(!exec.in)
        {
            exec.in = make_stream();
            exec.in->close();
        }
        if(!exec.control)
            exec.control = std::make_shared<ProcessControl>();

        return m_scheduler(runRawCommand(exec), exec);
    }

/**
     * @brief runRawCommand
     * @param exec
     * @return
     *
     * This command searches for the appropriate command
     * in the command list, executes the coroutine and
     * returns the task.
     *
     * You must explicitally execute the task yourself
     * in your own scheduler.
     *
     * This function ensures that the output stream
     * is closed when the command completes.
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
     */
    task_type runRawCommand(Exec exec)
    {
        auto & args = exec.args;
        exec.mini = this;

        auto it = funcs.find(args[0]);
        if(it ==  funcs.end())
            co_return 127;

        auto T = it->second(exec);

        //std::cout << "Running: " << args[0] << std::endl;
        while (!T.done()) {
            co_await std::suspend_always{};
            T.resume();
        }

        if(exec.out)
        {
            exec.out->close();
        }

        //std::cout << "Finished: " << args[0] << std::endl;
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
        funcs["help"] = [this](Exec args) -> task_type
        {
            args << "List of commands:\n\n";
            for(auto & f : funcs)
            {
                args << f.first << '\n';
            }
            co_return 0;
        };
        funcs["env"] = [this](Exec args) -> task_type
        {
            for(auto & [var, val] : args.env)
            {
                args << std::format("{}={}\n", var,val);
            }
            co_return 0;
        };
        funcs["echo"] = [](Exec args) -> task_type
        {
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

            args << std::to_string(i) << '\n';
            //std::cout << std::to_string(i);
            co_return 0;
        };
    }

};


}


#endif
