#ifndef MINI_LINUX_H
#define MINI_LINUX_H

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <future>

#include "ReaderWriterStream.h"
#include "task.h"

#include  <sys/ioctl.h>

namespace bl
{

template <typename Container>
std::string join(const Container& c, const std::string& delimiter = ", ") {
    std::ostringstream oss;
    auto it = c.begin();
    if (it != c.end()) {
        oss << *it;
        ++it;
    }
    for (; it != c.end(); ++it) {
        oss << delimiter << *it;
    }
    return oss.str();
}

struct MiniLinux
{
    using stream_type      = ReaderWriterStream_t<char>;
    using pid_type         = uint32_t;
    using return_code_type = int32_t;
    using task_type        = gul::Task_t<return_code_type, std::suspend_always, std::suspend_always>;


    struct ProcessControl
    {
        std::vector<std::string>           args;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::map<std::string, std::string> env;

        pid_type    pid = 0xFFFFFFFF;
        bool   sig_kill = false;
        MiniLinux *mini = nullptr;

        void setSignalHandler(std::function<void(int)> f)
        {
            mini->m_procs2.at(pid).signal = f;
        }

        bool is_sigkill() const
        {
            if(sig_kill)
            {
                //std::cerr << "Pid killed: " << pid << std::endl;
            }
            return sig_kill;
        }
        pid_type get_pid() const
        {
            return pid;
        }

        ProcessControl& operator << (std::string_view const &ss)
        {
            for(auto i : ss)
            {
                out->put(i);
            }
            return *this;
        }
        ProcessControl& operator << (char d)
        {
            out->put(d);
            return *this;
        }

        template<typename iter_container>
            requires std::ranges::range<iter_container>
        ProcessControl& operator << (iter_container const & d)
        {
            for(auto i : d)
            {
                out->put(i);
            }
            return *this;
        }

        template<typename iter_container>
            requires std::ranges::range<iter_container>
        ProcessControl& operator >> (iter_container const & d)
        {
            while(has_data())
                d.insert(d.end(), get());
            return *this;
        }

        bool has_data() const
        {
            return in->has_data();
        }

        char get()
        {
            return in->get();
        }
        int32_t put(char c)
        {
            out->put(c);
            return 1;
        }
        bool eof() const
        {
            return in->eof();
        }
        size_t readsome(char *c, size_t i)
        {
            size_t j=0;
            while(has_data() && j<i)
            {
                *c = get();
                ++c;
                j++;
            }
            return j;
        }
    };

    struct Exec
    {
        std::vector<std::string>           args;
        std::map<std::string, std::string> env;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;

        Exec(std::vector<std::string> const &_args = {}, std::map<std::string, std::string> const & _env = {}) : args(_args), env(_env)
        {
        }
    };

    static Exec parseArguments(std::vector<std::string> args)
    {
        Exec e;
        e.args = args;
        // cycle through the arguments and search for the first
        // element that does not have the following pattern:
        //
        // VARNAME=VARVALUE
        //
        // As the patterns are found, remove them and place
        // them in the environment map
        auto it = std::find_if(e.args.begin(), e.args.end(), [&e](auto & arg)
                                    {
                                        auto [var,val] = splitVar(arg);
                                        if(!var.empty())
                                        {
                                            e.env[std::string(var)] = val;
                                            return false;
                                        }
                                        return true;
        });
        e.args.erase(e.args.begin(), it);

        return e;
    }

    /**
     * @brief genPipeline
     * @param array_of_args
     * @return
     *
     * Given a vector of argument lists, generte a vector of exec objects
     * where one cmd is piped into the next
     *
     */
    static std::vector<Exec> genPipeline( std::vector<std::vector<std::string> > array_of_args)
    {
        std::vector<Exec> out;

        for(size_t i=0;i<array_of_args.size();i++)
        {
            out.push_back(parseArguments(array_of_args[i]));
            out.back().out = make_stream();
        }
        for(size_t i=1;i<out.size();i++)
        {
            out[i].in = out[i-1].out;
        }
        return out;
    }

    using e_type = std::shared_ptr<ProcessControl>;
    using function_type    = std::function< task_type(e_type)>;

    void clearFunction(std::string name)
    {
        m_funcs.erase(name);
    }
    void setFunction(std::string name, std::function< task_type(e_type) > _f)
    {
        m_funcs[name] = _f;
    }


    static std::pair<std::string_view, std::string_view> splitVar(std::string_view var_def)
    {
        auto i = var_def.find_first_of('=');
        if(i!=std::string::npos)
        {
            return {{&var_def[0],i}, {&var_def[i+1], var_def.size()-i-1}};
        }
        return {};
    };

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
    pid_type runRawCommand(Exec args)
    {
        // Try to find the name of the function to run
        auto it = m_funcs.find(args.args[0]);
        if(it ==  m_funcs.end())
            return 0xFFFFFFFF;

        auto & exec_args = args;

        if(!exec_args.in)
        {
            exec_args.in = make_stream();
            //exec_args.in->close();
        }

        if(m_preExec)
            m_preExec(exec_args);

        if(!args.out) args.out = make_stream();
        if(!args.in) args.in = make_stream();

        auto proc_control = std::make_shared<ProcessControl>();
        proc_control->args = args.args;
        proc_control->in   = args.in;
        proc_control->out  = args.out;
        proc_control->env  = args.env;


        // run the function, it is a coroutine:
        // it will return a task}
        auto T = it->second(proc_control);

        auto pid = registerProcess(std::move(T), std::move(proc_control));

        return pid;
    }

    // register a task as a process by giving it a PID
    // and placing it in the scheduler to be run
    //
    pid_type registerProcess(task_type && t, e_type arg, pid_type parent = 0xFFFFFFFF)
    {
        auto _pid = _pid_count++;
        if(arg == nullptr)
            arg = std::make_shared<ProcessControl>();

        Process _t = {arg, std::move(t)};

        _t.parent = parent;
        arg->pid = _pid;
        arg->mini = this;

        m_procs2.emplace(_pid, std::move(_t));

        return _pid;
    }


    std::vector<pid_type> runPipeline(std::vector<Exec> E)
    {
        if(!E.front().in)
            E.front().in = make_stream();
        if(!E.back().out)
            E.back().out = make_stream();

        for(size_t i=0;i<E.size()-1;i++)
        {
            assert(E[i].out == E[i+1].in);
        }

        std::vector<pid_type> out;
        for(auto & e  : E)
        {
            out.push_back(runRawCommand(e));
        }
        return out;
    }

    /**
     * @brief setStdIn
     * @param pid
     * @param stream
     * @return
     *
     * Sets the standard input of a PID to a specific stream
     */
    bool setStdIn(pid_type pid, std::shared_ptr<stream_type> stream)
    {
        auto it = m_procs2.find(pid);
        if(it == m_procs2.end()) return false;
        it->second.control->in = stream;
        return true;
    }

    /**
     * @brief isRunning
     * @param pid
     * @return
     *
     *
     * Checks if a specific pid is running
     */
    bool isRunning(pid_type pid) const
    {
        auto it = m_procs2.find(pid);
        if(it == m_procs2.end()) return false;
        return !it->second.is_complete;
    }

    bool isAllComplete(std::vector<pid_type> const &pid) const
    {
        for(auto & p : pid)
        {
            if(isRunning(p)) return false;
        }
        return true;
    }


    bool signal(pid_type pid, int sigtype)
    {
        if(isRunning(pid))
        {
            auto & proc = m_procs2.at(pid);
            if(proc.signal)
            {
                proc.signal(sigtype);
            }

            return true;
        }
        return false;
    }
    /**
     * @brief kill
     * @param pid
     *
     * Kill a running pid
     */
    bool kill(pid_type pid, bool dash_9=false)
    {
        if(isRunning(pid))
        {
            if(dash_9)
            {
                m_procs2.erase(pid);
            }
            signal(pid, 2);
            m_procs2.at(pid).control->sig_kill=true;
            return true;
        }
        return false;
    }

    /**
     * @brief getIO
     * @param pid
     * @return
     *
     * Returns the input and output streams for a process
     */
    std::pair<std::shared_ptr<stream_type>, std::shared_ptr<stream_type>> getIO(pid_type pid)
    {
        if(isRunning(pid))
        {
            return {m_procs2.at(pid).control->in, m_procs2.at(pid).control->out};
        }
        return {};
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
        if(coro.is_complete)
            return true;

        if(!coro.task.done())
        {
            //std::cerr << "Resuming: " << coro.control->args[0] << std::endl;
            auto t0 = std::chrono::high_resolution_clock::now();
            coro.task.resume();
            auto t1 = std::chrono::high_resolution_clock::now();
            coro.processTime += std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0);
        }
        if(coro.task.done())
        {
            auto exit_code = coro.task();
            coro.is_complete = true;
            *coro.exit_code = exit_code;
            coro.control->env["?"] = std::to_string(exit_code);
            coro.control->pid = pid;

            if(coro.control->out)
            {
                coro.control->out->close();
            }

            return true;
        }
        return false;
    }

    /**
     * @brief executeAll
     * @return
     *
     * Executes all the processes and returns the total
     * number of processes still in the scheduler
     */
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

    /**
     * @brief processExitCode
     * @param p
     * @return
     *
     * Get a pointer to the exit code for the pid. This value
     * will be set to -1 if the process has not completed
     * Or will be nullptr if the process doesn't exist
     */
    std::shared_ptr<return_code_type> processExitCode(pid_type p) const
    {
        if(auto it = m_procs2.find(p); it!=m_procs2.end())
        {
            return it->second.exit_code;
        }
        return nullptr;
    }

    //std::function< std::future<int>(task_type&&) > m_scheduler;
    std::function< void(Exec&) >                 m_preExec;
    std::function< void(Exec&) >                 m_postExec;

    struct Process
    {
        std::shared_ptr<ProcessControl> control;
        task_type                       task;
        pid_type                        parent = 0xFFFFFFFF;

        bool is_complete = false;
        std::shared_ptr<return_code_type> exit_code = std::make_shared<return_code_type>(-1);
        std::function<void(int)>        signal = {};
        std::chrono::nanoseconds        processTime;
    };

    std::shared_ptr<ProcessControl> getProcessControl(pid_type pid)
    {
        return m_procs2.at(pid).control;
    }
    std::map<std::string, std::function< task_type(e_type) >> m_funcs;
    std::map<uint32_t, Process > m_procs2;
protected:

    pid_type _pid_count=1;

    void setDefaultFunctions()
    {
        m_funcs["false"] = [](e_type ctrl) -> task_type
        {
            (void)ctrl;
            co_return 1;
        };
        m_funcs["true"] = [](e_type ctrl) -> task_type
        {
            (void)ctrl;
            co_return 0;
        };
        m_funcs["help"] = [this](e_type ctrl) -> task_type
        {
            auto & arg = *ctrl;
            arg << "List of commands:\n\n";
            for(auto & f : m_funcs)
            {
                arg << f.first << '\n';
            }
            co_return 0;
        };
        m_funcs["env"] = [this](e_type args) -> task_type
        {
            for(auto & [var, val] : args->env)
            {
                *args << std::format("{}={}\n", var,val);
            }
            co_return 0;
        };
        m_funcs["echo"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            for(size_t i=1;i<args.args.size();i++)
            {
                args << std::format("{}{}", args.args[i], (i==args.args.size()-1 ? "\n" : " "));
            }
            co_return 0;
        };
        m_funcs["sleep"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
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
            while(!args.is_sigkill() && std::chrono::system_clock::now() < T1)
            {
                co_await std::suspend_always{};
            }
            co_return 0;
        };
        m_funcs["rev"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            std::string output;

            while(!args.is_sigkill())
            {
                while(args.has_data())
                {
                    output.push_back(args.get());
                    if(output.back() == '\n')
                    {
                        output.pop_back();
                        std::reverse(output.begin(), output.end());
                        args << output << '\n';
                        output.clear();
                    }
                }
                if(args.eof())
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

        m_funcs["wc"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            uint32_t i=0;

            while(!args.is_sigkill())
            {
                while(!args.eof() && args.has_data())
                {
                    args.get();
                    ++i;
                }
                if(args.eof())
                    break;
                else
                    co_await std::suspend_always{};
            }

            args << std::to_string(i) << '\n';
            //std::cout << std::to_string(i);
            co_return 0;
        };
        m_funcs["ps"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            auto & M = *args.mini;

            args << std::format("PID   CMD\n");
            for(auto & [pid, P] : M.m_procs2)
            {
                std::string cmd;
                for(auto & c : P.control->args)
                    cmd += c + " ";
                args << std::format("{}     {}\n", pid, cmd);
            }

            //std::cout << std::to_string(i);
            co_return 0;
        };
        m_funcs["kill"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            if(args.args.size() < 2)
                co_return 1;

            pid_type pid = 0;
            auto [ptr, ec] = std::from_chars(args.args[1].data(), args.args[1].data() + args.args[1].size(), pid);
            (void)ec;
            (void)ptr;
            auto & M = *args.mini;
            if(!M.kill(pid))
            {
                args << std::format("Could not find process ID: {}\n", pid);
                co_return 0;
            }
            co_return 1;
        };
        m_funcs["signal"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            if(args.args.size() < 3)
                co_return 1;

            pid_type pid = 0;
            int sig=2;
            {
                auto [ptr, ec] = std::from_chars(args.args[1].data(), args.args[1].data() + args.args[1].size(), pid);
                (void)ec;
                (void)ptr;
            }
            {
                auto [ptr, ec] = std::from_chars(args.args[2].data(), args.args[2].data() + args.args[2].size(), sig);
                (void)ec;
                (void)ptr;
            }
            auto & M = *args.mini;
            if(!M.signal(pid, sig))
            {
                args << std::format("Could not find process ID: {}\n", pid);
                co_return 0;
            }
            co_return 1;
        };
        m_funcs["io_info"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            for(auto & [pid, proc] : ctrl->mini->m_procs2)
            {
                args << std::format("{}[{}]->{}->{}[{}]\n", static_cast<void*>(proc.control->in.get()), proc.control->in.use_count(), proc.control->args[0], static_cast<void*>(proc.control->out.get()), proc.control->out.use_count() );
            }
            co_return 0;
        };

        m_funcs["launcher"] = [](bl::MiniLinux::e_type control) -> bl::MiniLinux::task_type
        {
            // This is launcher process.
            // You don't need to use this if you are building a GUI application
            // but if you are building a commandline app, then you'll need a way
            // to read stdin and direct it to to the sh process
            //
            auto & exev = *control;

            static auto count = 0;
            if(count != 0)
            {
                *exev.out << "Only one instance of fromCin can exist\n";
                co_return 1;
            }

            count++;

            if(exev.args.size() < 2)
            {
                std::cout << "Requires a command to be called\\nn";
                std::cout << "   launcher sh";
                co_return 1;
            }

            // Execute a raw command
            auto sh_pid = control->mini->runRawCommand(bl::MiniLinux::parseArguments({exev.args[1]}));
            if(sh_pid == 0xFFFFFFFF)
            {
                std::cout << "Invalid Command: " << exev.args[1] << "\n";
                co_return 1;
            }

            // Get the input and output streams for the
            // shell process
            auto [c_in, c_out] = control->mini->getIO(sh_pid);

            auto ff = std::shared_ptr<void*>(nullptr, [&](auto)
            {
                // Manually close the input and output streams for
                // the subprocess otherwise
                // the subprocess will continuiously wait on new
                // data
                c_in->close();
            });
            char buffer[1024];

            while (!exev.is_sigkill())
            {
                // std::getline blocks until data is entered, but
                // we dont want to do that because this will block our entire
                // process
                // we want to check if bytes are available and then
                // read them in, if no bytes are there, we should suspend the
                // coroutine
                int bytes = 0;
                // check if there are any bytes in stdin
                if (ioctl(STDIN_FILENO, FIONREAD, &bytes) == -1) {
                    co_return 1;
                }

                // Read all the bytes from standard input
                while(bytes > 0)
                {
                    int bytes_to_read = std::min(bytes, 1023);
                    std::cin.read(buffer, bytes_to_read);

                    // and pipe them into the
                    // output stream
                    for(int i=0;i<bytes_to_read;i++)
                        c_in->put(buffer[i]);

                    if (ioctl(STDIN_FILENO, FIONREAD, &bytes) == -1) {
                        co_return 1;
                    }
                }

                // If there are any bytes in the output stream of
                // sh, read them and write them to std::cout
                while(c_out->has_data())
                    std::cout.put( c_out->get());

                // Check if the sh function is still running
                // if not, quit.
                if(!control->mini->isRunning(sh_pid))
                {
                    co_return 0;
                }
                co_await std::suspend_always{};
            }

            if(exev.is_sigkill())
            {
                exev.mini->kill(sh_pid);
            }
            count--;

            co_return 0;
        };
    }

};


}


#endif

