#ifndef PSEUDONIX_SYSTEM_H
#define PSEUDONIX_SYSTEM_H

#include <vector>
#include <string>
#include <map>
#include <functional>
#include "ReaderWriterStream.h"
#include "task.h"

namespace PseudoNix
{

constexpr const int exit_interrupt  = 130;
constexpr const int exit_terminated = 143;

constexpr const int sig_interrupt  = 2;
constexpr const int sig_terminate = 15;


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

#define SUSPEND_POINT(C)         \
{                                \
if (C->sig_code == PseudoNix::sig_terminate)      \
co_return static_cast<int>(PseudoNix::exit_terminated);                   \
co_await std::suspend_always{};  \
}

#define SUSPEND_SIGTERM(C) \
{\
    if(C->sig_code == PseudoNix::sig_int )  { /*std::cout << "SIGINT" << std::endl; */ co_return static_cast<int>(exit_interrupt);} \
    if(C->sig_code == PseudoNix::sig_term ) { /*std::cout << "SIGTERM" << std::endl;*/ co_return static_cast<int>(exit_terminated);} \
        co_await std::suspend_always{};\
}

#define SUSPEND_SIG_TERM     SUSPEND_POINT
#define SUSPEND_SIG_INT_TERM SUSPEND_SIGTERM

struct System
{
    using stream_type      = ReaderWriterStream_t<char>;
    using pid_type         = uint32_t;
    using return_code_type = int32_t;
    using task_type        = Task_t<return_code_type, std::suspend_always, std::suspend_always>;


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

    class Awaiter {
    public:
        explicit Awaiter(pid_type p, System* S,std::function<bool(void)> f)
            : m_pid(p), m_system(S), m_pred(f)
        {
            // std::cerr << "Sleep Awaiter Created: " << this << std::endl;
        }

        ~Awaiter()
        {
            // std::cerr << "Sleep Awaiter Destroyed: " << this << std::endl;
        }

        // called to check if
        bool await_ready() const noexcept {
            auto b = m_pred();
            //std::cerr << "await_ready called: " << this << "  " << b << std::endl;
            return b;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            handle_ = handle;

            // this is where we need to place
            // the handle for executing on another
            // scheduler
            assert(m_system->m_procs2.at(m_pid).awaiter == nullptr);
            m_system->m_procs2.at(m_pid).awaiter = this;
        }

        int32_t await_resume() const noexcept {
            return m_signal_type;
        }

        void resume()
        {
            //std::cerr << "Awaiter resuming handle: " << handle_.address() << std::endl;
            if(handle_)
            {
                handle_.resume();
                handle_ = {};
            }
        }

        void set_signal_code(int32_t d)
        {
            m_signal_type = d;
        }
    protected:
        pid_type m_pid;
        System * m_system;
        std::function<bool()> m_pred;
        std::coroutine_handle<> handle_;
        int32_t m_signal_type = 0;
    };


    struct ProcessControl
    {
        std::vector<std::string>           args;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::map<std::string, std::string> env;

        pid_type    pid = 0xFFFFFFFF;
        //bool   sig_kill = false;
        //bool   sig_int  = false;

        int32_t   sig_code = 0;
        System *mini = nullptr;

        void setSignalHandler(std::function<void(int)> f)
        {
            mini->m_procs2.at(pid).signal = f;
        }

        pid_type get_pid() const
        {
            return pid;
        }

        System::Awaiter await_yield()
        {
            return System::Awaiter{pid,
                    mini,
                    [x=false]() mutable {
                        if(!x)
                        {
                            x = true;
                            // retun false the first time
                            // so that it will immediately suspend
                            return false;
                        }
                        // subsequent times, return true
                        // so that we know it
                        return true;
                        }};
        }

        System::Awaiter await_data(System::stream_type *d)
        {
            return System::Awaiter{get_pid(),
                                   mini,
                                   [d](){
                                       return d->has_data();
                                   }};
        }

        System::Awaiter await_yield_for(std::chrono::nanoseconds time)
        {
            auto T1 = std::chrono::system_clock::now() + time;
            return System::Awaiter{get_pid(),
                                   mini,
                                   [T=T1](){
                                       return std::chrono::system_clock::now() > T;
                                   }};
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

        ProcessControl& operator << (char const *d)
        {
            while(*d != 0)
            {
                out->put(*d);
                ++d;
            }
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

        std::vector<pid_type> subProcesses;

        pid_type executeSubProcess(System::Exec E)
        {
            auto p = mini->runRawCommand(E, get_pid());
            this->subProcesses.push_back(p);
            return p;
        }
        std::vector<pid_type> executeSubPipeline(std::vector<Exec> E)
        {
            auto pids = mini->runPipeline(E, get_pid());
            this->subProcesses.insert(this->subProcesses.end(), pids.begin(), pids.end());
            return pids;
        }

        bool areSubProcessesFinished() const
        {
            for(auto p : subProcesses)
            {
                if( mini->isRunning(p) ) return false;
            }
            return true;
        }
        void signalSubProcesses(int32_t signal)
        {
            for(auto p : subProcesses)
            {
                mini->signal(p, signal);
            }
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


    System()
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
    pid_type runRawCommand(Exec args, pid_type parent = 0xFFFFFFFF)
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

        auto pid = registerProcess(std::move(T), std::move(proc_control), parent);

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

        std::weak_ptr<ProcessControl> p = arg;

        // default signal handler
        // is to pass all signals to child processes
        _t.signal = [p, _signaled = std::make_shared<bool>(false)](int s)
        {
            if(*_signaled)
                return;
            *_signaled = true;
            if(auto aa = p.lock(); aa)
            {
                aa->sig_code = s;
                std::cerr << std::format("[{}] Default Signal Handler: {}", s, join(aa->args)) << std::endl;
                aa->signalSubProcesses(s);
            }
            *_signaled = false;
        };
        m_procs2.emplace(_pid, std::move(_t));

        return _pid;
    }


    std::vector<pid_type> runPipeline(std::vector<Exec> E, pid_type parent = 0xFFFFFFFF)
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
            out.push_back(runRawCommand(e,parent));
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
                proc.control->sig_code = sigtype;
                proc.signal(sigtype);
            }

            return true;
        }
        return false;
    }

    /**
     * @brief kill
     * @param pid
     * @return
     *
     * Forcefully kill the running processs. The
     * process's output streams will be closed and
     * its coroutine will be removed from the scheduler.
     * Your process will not be able to clean up any
     * resources.
     */
    bool kill(pid_type pid)
    {
        if(isRunning(pid))
        {
            auto & proc = m_procs2.at(pid);
            proc.force_terminate = true;
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
            // check if the coroutine has an awaiter
            // that needs to be processed
            if(coro.awaiter)
            {
                // Process the awaiter. The awaiter has
                // an internal function object that can executed
                if(coro.control->sig_code != 0 || coro.awaiter->await_ready())
                {
                    //if(coro.control->sig_code!=0) std::cerr << "Forcing resume due to signal: " << pid << std::endl;
                    auto a = coro.awaiter;
                    a->set_signal_code(coro.control->sig_code);
                    // if the coroutine is ready to be resumed
                    // null out the awaiter, because it is possible
                    // that resuming the coroutine will produce another
                    // awaiter
                    coro.awaiter = nullptr;
                    //std::cerr << "Scheduler resuming awaiter: " << a-> << std::endl;
                    //std::cerr << "Scheduler resuming awaiter: " << a << std::endl;
                    a->resume();
                }
            }
            else
            {
                auto t0 = std::chrono::high_resolution_clock::now();
                coro.task.resume();
                auto t1 = std::chrono::high_resolution_clock::now();
                coro.processTime += std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0);
            }
        }
        if(coro.task.done())
        {
            auto exit_code = coro.task();
            coro.is_complete = true;
            *coro.exit_code = exit_code;
            coro.control->env["?"] = std::to_string(exit_code);
            coro.control->pid = pid;
            coro.awaiter = nullptr;

            if(coro.control->out && coro.control.use_count() == 2)
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
        for(auto it = m_procs2.begin(); it!=m_procs2.end();it++)
        {
            auto & pid = it->first;
            if(execute(pid))
            {
                it->second.force_terminate = true;
                it->second.is_complete = true;
                it->second.control->out->close();
            }
        }

        // Remove any processes that have been completed
        // or have been forcefuly terminated
        for(auto it = m_procs2.begin(); it!=m_procs2.end();)
        {
            if(it->second.force_terminate)
            {
                it->second.control->out->close();

                it = m_procs2.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return m_procs2.size();
    }

    size_t executeAllFor(std::chrono::nanoseconds d, size_t maxIterations)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        size_t ret=0;
        size_t i=0;
        while(true)
        {
            ret = executeAll();
            if(std::chrono::high_resolution_clock::now()-t0 > d || i++ > maxIterations)
                break;
        }
        return ret;
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

        Awaiter                       * awaiter = nullptr;

        bool is_complete = false;
        std::shared_ptr<return_code_type> exit_code = std::make_shared<return_code_type>(-1);
        std::function<void(int)>          signal = {};
        std::chrono::nanoseconds          processTime = std::chrono::nanoseconds(0);

        bool force_terminate = false;
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
        m_funcs["help"] = [](e_type ctrl) -> task_type
        {
            auto & arg = *ctrl;
            arg << "List of commands:\n\n";
            for(auto & f : ctrl->mini->m_funcs)
            {
                arg << f.first << '\n';
            }
            co_return 0;
        };
        m_funcs["env"] = [](e_type args) -> task_type
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
        m_funcs["yes"] = [](e_type ctrl) -> task_type
        {
            // A very basic example of a forever running
            // process
            while(true)
            {
                *ctrl << "y\n";

                auto sig = co_await ctrl->await_yield();
                if(sig == PseudoNix::sig_interrupt ) { co_return static_cast<int>(PseudoNix::exit_interrupt);}
                if(sig == PseudoNix::sig_terminate ) { co_return static_cast<int>(PseudoNix::exit_terminated);}
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

            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            auto sig = co_await ctrl->await_yield_for(std::chrono::milliseconds( static_cast<uint64_t>(t*1000)));
            if(sig == PseudoNix::sig_interrupt ) { co_return static_cast<int>(PseudoNix::exit_interrupt);}
            if(sig == PseudoNix::sig_terminate ) { co_return static_cast<int>(PseudoNix::exit_terminated);}
            co_return 0;
        };

        m_funcs["uptime"] = [T0=std::chrono::system_clock::now()](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            args << std::format("{}\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-T0).count());
            co_return 0;
        };
        m_funcs["rev"] = [](e_type ctrl) -> task_type
        {
            auto & args = *ctrl;
            std::string output;

            while(true)
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
                    SUSPEND_POINT(ctrl)
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

            while(true)
            {
                auto sig = co_await ctrl->await_data(args.in.get());
                if(sig == PseudoNix::sig_interrupt ) { co_return static_cast<int>(PseudoNix::exit_interrupt);}
                if(sig == PseudoNix::sig_terminate ) { co_return static_cast<int>(PseudoNix::exit_terminated);}
                while(!args.eof())
                {
                    args.get();
                    ++i;
                }
                if(args.eof())
                    break;
            }

            args << std::to_string(i) << '\n';

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
    }
};


}


#endif

