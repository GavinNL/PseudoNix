#ifndef PSEUDONIX_SYSTEM_H
#define PSEUDONIX_SYSTEM_H

#include <vector>
#include <string>
#include <map>
#include <functional>
#include "ReaderWriterStream.h"
#include "task.h"
#include "defer.h"
#include <span>
#include <thread>

namespace PseudoNix
{

constexpr const int exit_interrupt  = 130;
constexpr const int exit_terminated = 143;
constexpr const uint32_t invalid_pid = 0xFFFFFFFF;

constexpr const int sig_interrupt  = 2;
constexpr const int sig_terminate = 15;

static std::pair<std::string_view, std::string_view> splitVar(std::string_view var_def)
{
    auto i = var_def.find_first_of('=');
    if(i!=std::string::npos)
    {
        return {{&var_def[0],i}, {&var_def[i+1], var_def.size()-i-1}};
    }
    return {};
};

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

enum class AwaiterResult
{
    SUCCESS = 0,
    SIGNAL_INTERRUPT = sig_interrupt,
    SIGNAL_TERMINATE = sig_terminate,
    UNKNOWN_ERROR
};


#if defined PSUEDONIX_ENABLE_DEBUG
#define DEBUG_LOG(...) std::cerr << std::format(__VA_ARGS__) << std::flush;
#else
#define DEBUG_LOG(...)
#endif

template <typename T, typename... Ts>
concept all_same = (std::same_as<T, Ts> && ...);

struct System
{
    using stream_type      = ReaderWriterStream_t<char>;
    using pid_type         = uint32_t;
    using exit_code_type   = int32_t;
    using task_type        = Task_t<exit_code_type, std::suspend_always, std::suspend_always>;


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
            m_signal = &m_system->m_procs2.at(p).lastSignal;
        }

        ~Awaiter()
        {
            // std::cerr << "Sleep Awaiter Destroyed: " << this << std::endl;
        }

        // called to check if
        bool await_ready() const noexcept {
            // Indicate that the awaiter is ready to be
            // resumed if we have internally set the
            // result to be a non-success
            if(static_cast<AwaiterResult>(*m_signal) != AwaiterResult::SUCCESS)
                return true;
            auto b = m_pred();
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

        AwaiterResult await_resume() const noexcept {
            return static_cast<AwaiterResult>(*m_signal);
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

    protected:
        pid_type m_pid;
        System * m_system;
        std::function<bool()> m_pred;
        std::coroutine_handle<> handle_;
        int32_t * m_signal = nullptr;
    };


    struct ProcessControl
    {
        friend struct System;

        std::vector<std::string>           args;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::map<std::string, std::string> env;
        System * system = nullptr;

    protected:
        pid_type    pid = invalid_pid;
    public:

        void setSignalHandler(std::function<void(int)> f)
        {
            system->m_procs2.at(pid).signal = f;
        }

        pid_type get_pid() const
        {
            return pid;
        }

        /**
         * @brief await_yield
         * @return
         *
         * Yield the current process until the
         * next iteration of the scheduler
         */
        System::Awaiter await_yield()
        {
            return System::Awaiter{pid,
                                   system,
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

        /**
         * @brief await_yield_for
         * @param time
         * @return
         *
         * Sleep for an amount of time.
         */
        System::Awaiter await_yield_for(std::chrono::nanoseconds time)
        {
            auto T1 = std::chrono::system_clock::now() + time;
            return System::Awaiter{get_pid(),
                                   system,
                                   [T=T1](){
                                       return std::chrono::system_clock::now() > T;
                                   }};
        }

        /**
         * @brief await_finished
         * @param _pid
         * @return
         *
         * Yield until a specific PID has completed
         */
        System::Awaiter await_finished(pid_type _pid)
        {
            return System::Awaiter{get_pid(),
                                   system,
                                   [_pid,sys=system]()
                                   {
                                       return !sys->isRunning(_pid);
                                   }};
        }

        /**
         * @brief await_finished
         * @param pids
         * @return
         *
         * Yield until all PIDs have completed
         */
        System::Awaiter await_finished(std::vector<pid_type> pids)
        {
            return System::Awaiter{get_pid(),
                                   system,
                                   [pids,sys=system]()
                                   {
                                       for(auto p : pids)
                                       {
                                           if( sys->isRunning(p) )
                                               return false;
                                       }
                                       return true;
                                   }};
        }

        /**
         * @brief await_read_line
         * @param d
         * @param line
         * @return
         *
         * Yield until a line has been read from the input stream. Similar to std::getline
         */
        System::Awaiter await_read_line(std::shared_ptr<System::stream_type> d, std::string & line)
        {
            return System::Awaiter{get_pid(),
                                   system,
                                   [d, l = &line]()
                                   {
                                       char c;
                                       while(true)
                                       {
                                           auto r = d->get(&c);
                                           switch(r)
                                           {
                                           case  System::stream_type::Result::EMPTY:
                                               return false;
                                           case  System::stream_type::Result::END_OF_STREAM:
                                               return true;
                                           case  System::stream_type::Result::SUCCESS:
                                               l->push_back(c);
                                               if(l->back() == '\n') { l->pop_back(); return true;};
                                               break;
                                           }
                                       }
                                       return false;
                                   }};
        }

        /**
         * @brief await_has_data
         * @param d
         * @return
         *
         * Yield until the input stream has data
         */
        System::Awaiter await_has_data(std::shared_ptr<System::stream_type> d)
        {
            return System::Awaiter{get_pid(),
                                   system,
                                   [d](){
                                       if(d->check() == System::stream_type::Result::EMPTY)
                                           return false;
                                       return true;
                                   }};
        }

        pid_type executeSubProcess(System::Exec E)
        {
            return system->runRawCommand(E, get_pid());
        }
        std::vector<pid_type> executeSubProcess(std::vector<Exec> E)
        {
            return system->runPipeline(E, get_pid());
        }
    };

    using e_type = std::shared_ptr<ProcessControl>;
    using function_type    = std::function< task_type(e_type)>;

    void removeFunction(std::string name)
    {
        m_funcs.erase(name);
    }
    void setFunction(std::string name, std::function< task_type(e_type) > _f)
    {
        m_funcs[name] = _f;
    }
    void removeAllFunctions()
    {
        m_funcs.clear();
    }

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
     * @brief spawnProcess
     * @param args
     * @return
     *
     * Spawn a process into the system and return the
     * pid that was generated
     *
     * auto pid = system.spawnProcess({"echo", "hello", "world"});
     *
     * You can set environemnt variables for the process using:
     *
     * auto pid = system.spawnProcess({"VAR=VALUE", "echo", "hello", "world"});
     */
    pid_type spawnProcess(std::vector<std::string> args)
    {
        return runRawCommand(parseArguments(args));
    }

    /**
     * @brief spawnPipelineProcess
     * @param args
     * @return
     *
     * Run multiple processes by piping the output of one process into the input
     * of another. Returns a vector of PIDs
     *
     * The linux equivelant of: "echo hello world | rev"
     *
     * auto pids = spawnPipelineProcess({
     *      {"echo", "hello", "world"},
     *      {"rev"}
     * });
     */
    std::vector<pid_type> spawnPipelineProcess(std::vector<std::vector<std::string>> args)
    {
        return runPipeline(genPipeline(args));
    }

    /**
     * @brief interrupt
     * @param pid
     * @return
     *
     * Send an interrupt signal to the
     * process. Unless your process has created
     * custom behaviour for interrupts. It will
     * exit the process.
     *
     * The process is not immediately interrupted.
     * The coroutine will be interrupted on its
     * next iteration.
     */
    bool interrupt(pid_type pid)
    {
        return signal(pid, sig_interrupt);
    }

    /**
     * @brief kill
     * @param pid
     * @return
     *
     * Forcefully kill the running processs. The
     * process's output streams will be closed and
     * its coroutine will be removed from the scheduler.
     * Your process will not be able to exit gracefully
     *
     * Killing the process does not happen immediately
     * the process will be killed on the next iteration
     * of the scheduler.
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
    pid_type runRawCommand(Exec args, pid_type parent = invalid_pid)
    {
        if(m_main_thread_id != std::thread::id{})
        {
            if(m_main_thread_id != std::this_thread::get_id())
            {
                throw std::runtime_error("Cannot execute new commands on a separate thread");
            }
        }
        // Try to find the name of the function to run
        auto it = m_funcs.find(args.args[0]);
        if(it ==  m_funcs.end())
            return invalid_pid;

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

    std::vector<pid_type> runPipeline(std::vector<Exec> E, pid_type parent = invalid_pid)
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

    // register a task as a process by giving it a PID
    // and placing it in the scheduler to be run
    // This is an internal function and shouldn't be used
    //
    pid_type registerProcess(task_type && t, e_type arg, pid_type parent = invalid_pid)
    {
        auto _pid = _pid_count++;
        if(arg == nullptr)
            arg = std::make_shared<ProcessControl>();

        Process _t = {arg, std::move(t)};

        _t.parent = parent;
        arg->pid = _pid;
        arg->system = this;

        if(parent != invalid_pid)
        {
            m_procs2.at(parent).child_processes.push_back(_pid);
        }

        std::weak_ptr<ProcessControl> p = arg;

        // default signal handler
        // is to pass all signals to child processes
        _t.signal = [p, this](int s)
        {
            if(auto aa = p.lock(); aa)
            {
                // Default signal handler will pass through the
                // signal to its children
                std::cerr << std::format("[{}] Default Signal Handler: {}", s, join(aa->args)) << std::endl;

                for(auto c : this->m_procs2.at(aa->pid).child_processes)
                {
                    this->signal(c, s);
                }
            }
        };
        m_procs2.emplace(_pid, std::move(_t));

        return _pid;
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


    /**
     * @brief signal
     * @param pid
     * @param sigtype
     * @return
     *
     * Sends a given signal to the pid.
     * The signal can be any int type, but we're
     * using 2 for interrupts, and 15 of Terminate
     * to be in consistant with linux.
     *
     * Your process can set a custom signal handler
     * to be called. The default signal handler
     * will signal all child processes
     *
     * When you signal a process, it will stay
     * in the signaled state
     */
    bool signal(pid_type pid, int sigtype)
    {
        if(isRunning(pid))
        {
            auto & proc = m_procs2.at(pid);
            if(proc.signal && !proc.has_been_signaled)
            {
                proc.has_been_signaled = true;
                proc.lastSignal = sigtype;
                proc.signal(sigtype);
                proc.has_been_signaled = false;
            }

            return true;
        }
        return false;
    }

    /**
     * @brief clearSignal
     * @param pid
     *
     * Clear the current signal. You can use this
     * if you are defining a custom signal handler
     */
    void clearSignal(pid_type pid)
    {
        if(isRunning(pid))
        {
            auto & proc = m_procs2.at(pid);
            proc.lastSignal = 0;
        }
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
    bool execute(pid_type pid)
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
                if(coro.awaiter->await_ready())
                {
                    auto a = coro.awaiter;
                    // if the coroutine is ready to be resumed
                    // null out the awaiter, because it is possible
                    // that resuming the coroutine will produce another
                    // awaiter
                    coro.awaiter = nullptr;

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
            *coro.exit_code = !coro.force_terminate ? exit_code : -1;
            coro.control->env["?"] = std::to_string(exit_code);
            coro.awaiter = nullptr;
            coro.force_terminate = true;

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
        m_main_thread_id = std::this_thread::get_id();

        for(auto it = m_procs2.begin(); it!=m_procs2.end();it++)
        {
            auto & pid = it->first;
            if(execute(pid))
            {
                it->second.force_terminate = true;
                it->second.is_complete = true;

                // destroy the task so that all
                // internal data is destroyed
                // and any defer blocks are executed
                it->second.task.destroy();

                // set the eof after destruction
                it->second.control->out->set_eof();
            }
        }

        // Remove any processes that have been completed
        // or have been forcefuly terminated
        for(auto it = m_procs2.begin(); it!=m_procs2.end();)
        {
            auto & coro = it->second;
            if(coro.force_terminate)
            {

                if(coro.parent != invalid_pid && m_procs2.count(coro.parent))
                {
                    auto & cp = m_procs2.at(coro.parent).child_processes;
                    cp.erase(std::remove_if(cp.begin(), cp.end(), [pid=it->first](auto && ch)
                                  {
                                      return ch==pid;
                                  }), cp.end());
                    coro.parent = invalid_pid;
                }

                // Destroy the task first before erasing everything
                // this is so any defered blocks in the coroutine
                // get run before the streams get closed
                if(it->second.task.valid()) it->second.task.destroy();

                if(coro.control->out && coro.control.use_count() == 2)
                {
                    coro.control->out->set_eof();
                }
                std::cerr << "Terminating: " << std::format("{}", join(coro.control->args)) << std::endl;
                it = m_procs2.erase(it);
            }
            else
            {
                ++it;
            }
        }
        m_main_thread_id = {};
        return m_procs2.size();
    }

    /**
     * @brief executeAllFor
     * @param d
     * @param maxIterations
     * @return
     *
     * Continuiously execute all processes in the scheduler
     * until the desired duration has been reached or the maxIterations
     * has been run.
     */
    size_t executeAllFor(std::chrono::nanoseconds d, size_t maxIterations)
    {
        auto T1 = std::chrono::high_resolution_clock::now()+d;
        size_t ret=0;
        size_t i=0;
        while(true)
        {
            ret = executeAll();
            if(std::chrono::high_resolution_clock::now() > T1 || i++ > maxIterations)
                break;
        }
        return ret;
    }
    /**
     * @brief getProcessExitCode
     * @param p
     * @return
     *
     * Get a pointer to the exit code for the pid. This value
     * will be set to -1 if the process has not completed
     * Or will be nullptr if the process doesn't exist.
     *
     * auto p = sys.spawnProcess({"sleep", "10"});
     * auto _exit = sys.getProcessExitCode(p);
     *
     */
    std::shared_ptr<exit_code_type> getProcessExitCode(pid_type p) const
    {
        if(auto it = m_procs2.find(p); it!=m_procs2.end())
        {
            return it->second.exit_code;
        }
        return nullptr;
    }

    /**
     * @brief getProcessControl
     * @param pid
     * @return
     *
     * Returns the process control for a specific PID
     */
    std::shared_ptr<ProcessControl> getProcessControl(pid_type pid)
    {
        return m_procs2.at(pid).control;
    }

    pid_type getParentProcess(pid_type pid)
    {
        return m_procs2.at(pid).parent;
    }

    /**
     * @brief parseArguments
     * @param args
     * @return
     *
     * Given a list of arguments: eg {"echo", "hello", "world"}
     *
     * Return an Exec object which can be inserted into the
     * the System to be executed concurrently.
     *
     * Environment variables can be by set for the command
     * by adding them before the command to run
     *
     * {"USER=BOB", "PASSWORD=hello", "cmd", "arg1"}
     *
     */
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


    std::function< void(Exec&) >                 m_preExec;

    struct Process
    {
        std::shared_ptr<ProcessControl> control;
        task_type                       task;

        Awaiter                       * awaiter = nullptr;

        bool is_complete = false;
        std::shared_ptr<exit_code_type  > exit_code = std::make_shared<exit_code_type>(-1);
        std::function<void(int)>          signal = {};
        std::chrono::nanoseconds          processTime = std::chrono::nanoseconds(0);

        // This is where the current signal is
        int32_t lastSignal = 0;
        bool has_been_signaled = false;
        bool force_terminate = false;

        pid_type                        parent = invalid_pid;
        std::vector<pid_type>           child_processes = {};
    };


protected:
    std::map<std::string, std::function< task_type(e_type) >> m_funcs;
    std::map<pid_type, Process > m_procs2;
    std::thread::id m_main_thread_id = {};
    pid_type _pid_count=1;

    void setDefaultFunctions()
    {
#define HANDLE_AWAIT_INT_TERM(returned_signal, CTRL)\
        switch(returned_signal)\
            {\
                case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT:  { co_return static_cast<int>(PseudoNix::exit_interrupt);}\
                case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}\
                    default: break;\
            }

#define HANDLE_AWAIT_BREAK_ON_SIGNAL(returned_signal, CTRL)\
            if(returned_signal == PseudoNix::AwaiterResult::SIGNAL_INTERRUPT)  { break;}\
            if(returned_signal == PseudoNix::AwaiterResult::SIGNAL_TERMINATE) { break;}

#define HANDLE_AWAIT_TERM(returned_signal, CTRL)\
        switch(returned_signal)\
            {\
                case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT:   CTRL->system->clearSignal(CTRL->get_pid()); break; \
                case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}\
                    default: break;\
            }

#define PSEUDONIX_PROC_START(control) \
            auto & COUT = *control->out; (void)COUT;\
            auto & CIN = *control->in; (void)CIN;\
            auto const PID  = control->get_pid(); (void)PID;\
            auto & SYSTEM = *control->system; (void)SYSTEM;\
            auto const & ARGS = control->args; (void)ARGS;\
            auto const & ENV = control->env; (void)ENV

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
            PSEUDONIX_PROC_START(ctrl);

            COUT << "List of commands:\n\n";
            for(auto & f : ctrl->system->m_funcs)
            {
                COUT << f.first << '\n';
            }
            co_return 0;
        };
        m_funcs["env"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            for(auto & [var, val] : ENV)
            {
                COUT << std::format("{}={}\n", var,val);
            }
            co_return 0;
        };
        m_funcs["echo"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            bool newline=true;
            // Handle -n option
            int start=1;
            if (ARGS.size() > 1 && std::string(ARGS[1]) == "-n") {
                newline = false;
                start=2;
            }

            COUT << std::format("{}", join(std::span(ARGS.begin()+start, ARGS.end()), " "));
            if(newline)
                COUT.put('\n');

            co_return 0;
        };
        m_funcs["yes"] = [](e_type ctrl) -> task_type
        {
            // A very basic example of a forever running
            // process
            PSEUDONIX_PROC_START(ctrl);

            while(true)
            {
                COUT << "y\n";

                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
            }

            co_return 0;
        };
        m_funcs["sleep"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            std::string output;
            if(ARGS.size() < 2)
                co_return 1;
            float t = 0.0f;
            std::istringstream in(ARGS[1]);
            in >> t;

            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(std::chrono::milliseconds( static_cast<uint64_t>(t*1000))), ctrl);

            co_return 0;
        };

        m_funcs["uptime"] = [T0=std::chrono::system_clock::now()](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);
            COUT << std::format("{}\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-T0).count());
            co_return 0;
        };
        m_funcs["rev"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            std::string output;
            bool quit = false;
            while(!quit)
            {
                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_has_data(ctrl->in), ctrl);

                switch(ctrl->in->read_line(output))
                {
                    case stream_type::Result::END_OF_STREAM:
                        quit = true;
                        [[fallthrough]];
                    case stream_type::Result::SUCCESS:
                    {
                        std::reverse(output.begin(), output.end());
                        *ctrl->out << std::format("{}\n", output);
                        output.clear();
                        break;
                    }
                    case stream_type::Result::EMPTY:
                        std::cerr << "FAILURE, should not be here" << std::endl;
                        break;
                }
            }
            co_return 0;
        };

        m_funcs["wc"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            uint32_t i=0;

            bool quit = false;
            while(!quit)
            {
                char c;
                co_await ctrl->await_has_data(ctrl->in);

                while(true)
                {
                    auto r = CIN.get(&c);
                    if(r == stream_type::Result::EMPTY)
                    {
                        break;
                    }
                    if(r == stream_type::Result::END_OF_STREAM)
                    {
                        quit = true;
                        break;
                    }
                    ++i;
                }
            }

            COUT << std::to_string(i) << '\n';

            co_return 0;
        };
        m_funcs["ps"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            COUT << std::format("PID   CMD\n");
            for(auto & [pid, P] : SYSTEM.m_procs2)
            {
                std::string cmd;
                for(auto & c : P.control->args)
                    cmd += c + " ";
                COUT<< std::format("{}     {}\n", pid, cmd);
            }

            //std::cout << std::to_string(i);
            co_return 0;
        };
        m_funcs["kill"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() < 2)
                co_return 1;

            pid_type pid = 0;
            if(std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pid).ec != std::errc())
            {
                COUT << std::format("Must be a Process ID. Recieved {}\n", ARGS[1]);
                co_return 1;
            }

            if(!SYSTEM.kill(pid))
            {
                COUT << std::format("Could not find process ID: {}\n", pid);
                co_return 0;
            }
            co_return 1;
        };
        m_funcs["signal"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() < 3)
                co_return 1;

            pid_type pid = 0;
            int sig=2;
            {
                if(std::errc() != std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pid).ec)
                {
                    COUT << std::format("Arg 1 must be a Process ID. Recieved {}\n", ARGS[1]);
                    co_return 1;
                }
            }
            {
                if(std::errc() != std::from_chars(ARGS[2].data(), ARGS[2].data() + ARGS[2].size(), sig).ec)
                {
                    COUT << std::format("Arg 2 must be a integer signal code. Recieved {}\n", ARGS[1]);
                    co_return 1;
                }
            }

            if(!SYSTEM.signal(pid, sig))
            {
                COUT << std::format("Could not find process ID: {}\n", pid);
                co_return 0;
            }
            co_return 1;
        };
        m_funcs["io_info"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            for(auto & [pid, proc] : SYSTEM.m_procs2)
            {
                COUT << std::format("{}[{}]->{}->{}[{}]\n", static_cast<void*>(proc.control->in.get()), proc.control->in.use_count(), proc.control->args[0], static_cast<void*>(proc.control->out.get()), proc.control->out.use_count() );
            }
            co_return 0;
        };
        m_funcs["to_std_cout"] = [](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);
            while(!CIN.eof())
            {
                std::string s;
                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_read_line(ctrl->in, s), ctrl);
                std::cout << s << std::endl;
            }
            co_return 0;
        };
    }
};


}


#endif

