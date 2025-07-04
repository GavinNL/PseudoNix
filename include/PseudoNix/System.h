#ifndef PSEUDONIX_SYSTEM_H
#define PSEUDONIX_SYSTEM_H

#define WIN32_LEAN_AND_MEAN
#define NOBOOL
#include <vector>
#include <string>
#include <map>
#include <functional>
#include "ReaderWriterStream.h"
#include <concurrentqueue.h>
#include "task.h"
#include "defer.h"
#include <span>
#include <thread>
#include <semaphore>
#include "FileSystem.h"
#include "helpers.h"


#define PSEUDONIX_VERSION_MAJOR 0
#define PSEUDONIX_VERSION_MINOR 1


#if defined PSEUDONIX_LOG_LEVEL_INFO
#define DEBUG_INFO(...) std::cerr << std::format("[info] " __VA_ARGS__) << std::endl;
#else
#define DEBUG_INFO(...)
#endif

#if defined PSEUDONIX_LOG_LEVEL_TRACE
#define DEBUG_TRACE(...) std::cerr << std::format("[trace] " __VA_ARGS__) << std::endl;
#else
#define DEBUG_TRACE(...)
#endif

#if defined PSEUDONIX_LOG_LEVEL_ERROR
#define DEBUG_ERROR(...) std::cerr << std::format("[error] " __VA_ARGS__) << std::endl;
#else
#define DEBUG_ERROR(...)
#endif

#if defined PSEUDONIX_LOG_LEVEL_SYSTEM
#define DEBUG_SYSTEM(...) std::cerr << std::format("[trace] " __VA_ARGS__) << std::endl;
#else
#define DEBUG_SYSTEM(...)
#endif

template <>
struct std::formatter<std::thread::id> : std::formatter<std::string> {
    auto format(std::thread::id id, auto& ctx) const {
        std::ostringstream oss;
        oss << id;
        return std::formatter<std::string>::format(oss.str(), ctx);
    }
};


namespace PseudoNix
{

constexpr const int exit_interrupt  = 130;
constexpr const int exit_terminated = 143;
constexpr const uint32_t invalid_pid = 0xFFFFFFFF;

constexpr const int sig_interrupt  = 2;
constexpr const int sig_terminate = 15;


/**
 * @brief The AwaiterResult enum
 *
 * This enum is returned when you coawait on the awaiter class.
 */
enum class AwaiterResult
{
    // No problems, the awaiter was resumed
    // successfully
    SUCCESS = 0,

    // The awaiter was resumed, but
    // the system sent a signal while it was
    // paused. You should terminate your process
    // gracefully
    SIGNAL_INTERRUPT = sig_interrupt,
    SIGNAL_TERMINATE = sig_terminate,

    // Sent if you are awaiting on an input stream
    // If it returns END_OF_STREAM, it means the
    // input stream was closed and you should
    // probably exit your process
    END_OF_STREAM,


    UNKNOWN_ERROR
};


struct System : public PseudoNix::FileSystem
{
    using stream_type      = ReaderWriterStream_t<char>;
    using pid_type         = uint32_t;
    using exit_code_type   = int32_t;
    using task_type        = Task_t<exit_code_type, std::suspend_always, std::suspend_always>;

    constexpr static const char * const DEFAULT_QUEUE = "MAIN";

    auto& PROC_AT(auto && key)
    {
        auto it = m_procs2.find(key);
        if(it == m_procs2.end())
        {
            throw std::runtime_error(std::format("Cannot find PID: {}", key));
        }
        return it->second;
    }

    struct Exec
    {
        std::vector<std::string>           args;
        std::map<std::string, std::string> env;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::string                        queue = DEFAULT_QUEUE;

        Exec(std::vector<std::string> const &_args = {}, std::map<std::string, std::string> const & _env = {}) : args(_args), env(_env)
        {
        }
    };

    /**
     * @brief The Awaiter class
     *
     * A global awaiter class. Specific behaviours are defined by the
     * function object, f, that is passed into the object.
     *
     * f is called whenever await_ready is called to check whether
     * the awaiter should continue to suspend. If f returns true
     * then the coroutine will not-suspend.
     */
    class Awaiter {
    public:
        /**
         * @brief Awaiter
         * @param p the pid of the coroutine process. Must be valid
         * @param S - a pointer to the system
         * @param f - the function object used to determine whether to resume or not
         * @param queuName - the queue that the coroutine should resume on
         *
         *
         */
        explicit Awaiter(pid_type p,
                         System* S,
                         std::function<bool(Awaiter*)> f,
                         std::string queuName = "")
            : m_pid(p), m_system(S), m_pred(f), m_queueName(queuName)
        {
            m_signal = &m_system->PROC_AT(p)->lastSignal;
        }

        Awaiter(){};

        ~Awaiter()
        {
            // std::cerr << "Sleep Awaiter Destroyed: " << this << std::endl;
        }

        // called to check if
        bool _firstRun = true;
        bool await_ready()  noexcept {
            // Indicate that the awaiter is ready to be
            // resumed if we have internally set the
            // result to be a non-success

            // Check if the signal has been triggered
            // If it has, we should stop waiting on
            // the awaiter. But only check
            // the signal if this is NOT the first run
            // of the awaiter check. ie:
            // if the signal had already been set for the
            // process, then when it calls
            //
            //  co_await ctrl->yield( )
            //
            // It wont yield to the next process
            //
            if(m_signal && !_firstRun)
            {
                switch(*m_signal)
                {
                case sig_interrupt: m_result = AwaiterResult::SIGNAL_INTERRUPT; m_signal = {}; return true; break;
                case sig_terminate: m_result = AwaiterResult::SIGNAL_TERMINATE; m_signal = {}; return true; break;
                default:
                    break;
                }
            }
            _firstRun = false;
            auto b = m_pred(this);
            return b;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            handle_ = handle;

            // this is where we need to place
            // the handle for executing on another
            // scheduler
            m_system->handleAwaiter(this);
        }

        void setResult(AwaiterResult r)
        {
            m_result = r;
        }

        [[ nodiscard ]] AwaiterResult await_resume() const noexcept {
            return m_result;
        }

        void resume()
        {
            if(handle_)
            {
                handle_.resume();
                //handle_ = {};
            }
        }

        auto get_pid() const
        {
            return m_pid;
        }

    protected:
        pid_type m_pid;
        System * m_system;
        std::function<bool(Awaiter*)> m_pred;
        int32_t * m_signal = nullptr;
        AwaiterResult m_result = {};
    public:
        std::coroutine_handle<> handle_;
        std::string m_queueName;
    };


    struct ProcessControl
    {
        friend struct System;

        std::vector<std::string>           args;
        std::shared_ptr<stream_type>       in;
        std::shared_ptr<stream_type>       out;
        std::map<std::string, std::string> env;        // environment variables
        std::map<std::string, bool>        exported;   // list of variables that are exported
        std::string                        queue_name = DEFAULT_QUEUE; // which queue to run on
        System * system = nullptr;
        path_type cwd = "/";

    protected:
        pid_type    pid = invalid_pid;
    public:

        bool chdir(path_type new_dir)
        {
            if(!system->exists(new_dir)) return false;
            if(!new_dir.has_root_directory())
            {
                cwd = cwd / new_dir;
                env["OLDPWD"] = env["PWD"];
                env["PWD"] = cwd.generic_string();
            }
            else
            {
                cwd = new_dir;
                env["OLDPWD"] = env["PWD"];
                env["PWD"] = cwd.generic_string();
            }
            return true;
        }
        void setSignalHandler(std::function<void(int)> f)
        {
            system->PROC_AT(pid)->signal = f;
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
        System::Awaiter await_yield(std::string_view queue=DEFAULT_QUEUE)
        {
            return System::Awaiter{pid,
                                   system,
                                   [x=false](Awaiter*) mutable {
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
                                   }, std::string(queue)};
        }

        /**
         * @brief await_yield_for
         * @param time
         * @return
         *
         * Sleep for an amount of time.
         */
        System::Awaiter await_yield_for(std::chrono::nanoseconds time, std::string_view queue=DEFAULT_QUEUE)
        {
            auto T1 = std::chrono::system_clock::now() + time;
            return System::Awaiter{get_pid(),
                                   system,
                                   [T=T1](Awaiter*){
                                       return std::chrono::system_clock::now() > T;
                                   }, std::string(queue)};
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
                                   [_pid,sys=system](Awaiter*)
                                   {
                                       return !sys->isRunning(_pid);
                                   }, std::string(queue_name)};
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
                                   [pids,sys=system](Awaiter*)
                                   {
                                       for(auto p : pids)
                                       {
                                           if( sys->isRunning(p) )
                                               return false;
                                       }
                                       return true;
                                   }, std::string(queue_name)};
        }

        /**
         * @brief await_read_line
         * @param d
         * @param line
         * @return
         *
         * Yield until a line has been read from the input stream. Similar to std::getline
         *
         * The awaiter will resume if:
         *   - A full line (ending with a newline character) has been found
         *      returns AwaiterResult::SUCCESS
         *
         *   - and END_OF_STREAM was found, (ie. the stream was closed)
         *   - the shared pointer only has 1 use_count.
         *      Returns AwaiterResult::END_OF_STREAM
         */
        System::Awaiter await_read_line(std::shared_ptr<System::stream_type> & d, std::string & line)
        {
            return System::Awaiter{get_pid(),
                                   system,
                                   [&d, l = &line](Awaiter* a)
                                   {
                                       char c;
                                       if(d.use_count() == 1 && !d->has_data())
                                       {
                                           // no one is writing to this stream
                                           // so by default it should be closed
                                           a->setResult(AwaiterResult::END_OF_STREAM);
                                           return true;
                                       }
                                       while(true)
                                       {
                                           auto r = d->get(&c);
                                           switch(r)
                                           {
                                           case  System::stream_type::Result::EMPTY:
                                               return false;
                                           case  System::stream_type::Result::END_OF_STREAM:
                                               a->setResult(AwaiterResult::END_OF_STREAM);
                                               return true;
                                           case  System::stream_type::Result::SUCCESS:
                                               l->push_back(c);
                                               if(l->back() == '\n') { l->pop_back(); return true;};
                                               break;
                                           }
                                       }
                                       return false;
                                   }, std::string(queue_name)};
        }

        /**
         * @brief await_has_data
         * @param d
         * @return
         *
         * Yield until the input stream has data
         */
        System::Awaiter await_has_data(std::shared_ptr<System::stream_type> & d)
        {
            return System::Awaiter{get_pid(),
                                   system,
                                   [&d](Awaiter* a){
                                       if(d.use_count() == 1 && !d->has_data() )
                                       {
                                           a->setResult(AwaiterResult::END_OF_STREAM);
                                           return true;
                                       }
                                       auto c = d->check();
                                       switch(c)
                                       {
                                           case  System::stream_type::Result::EMPTY:
                                               return false;
                                           case  System::stream_type::Result::END_OF_STREAM:
                                               a->setResult(AwaiterResult::END_OF_STREAM);
                                               return true;
                                           case  System::stream_type::Result::SUCCESS:
                                               return true;
                                       }
                                       return true;
                                   }, std::string(queue_name)};
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
    void setFunction(std::string name, std::string description, std::function< task_type(e_type) > _f)
    {
        m_funcs[name] = _f;
        spawnProcess({"help", "set", name, description});
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
        taskQueueCreate(DEFAULT_QUEUE);

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
            auto & proc = PROC_AT(pid);
            proc->force_terminate = true;
            return true;
        }
        return false;
    }

    /**
     * @brief terminateAll
     *
     * Terminate all running processes by sending a
     * SIG_TERM to all currently running processes.
     */
    void terminateAll(std::string queue_name = {})
    {       
        for(auto & [pid, P] : m_procs2)
        {
            if(P->control->queue_name == queue_name || queue_name.empty())
                signal(pid, sig_terminate);
        }
    }

    /**
     * @brief destroy
     * @return
     *
     * Destroy the System by sending a kill signal
     * too all running processes. Then
     * manually destroying them.
     */
    size_t destroy()
    {
        // First send the kill signal to all
        // processes that are running
        // to gracefully quit.
        //
        // The next time the queues are processesd
        // they should terminate themselves
        terminateAll();

        // run through all the queues and execute them
        // one at a time leaving the DEFAULT_QUEUE
        // for the final one to do clean up.
        // Do this 5 times. This should be enough time
        // to let the processes terminate
        for(size_t i=0;i<5;i++)
        {
            // go through each of the queues and
            // execute them
            for(auto & [queueName, queu] : m_awaiters)
            {
                if(queueName != DEFAULT_QUEUE)
                {
                    taskQueueExecute(queueName, std::chrono::milliseconds(25), 1);
                }
            }
            taskQueueExecute(DEFAULT_QUEUE , std::chrono::milliseconds(25), 1);

            if(process_count() == 0)
            {
                break;
            }
        }

        // at this point, any processes still running
        // should be forcefully killed.
        // send the KILL signal to all of them
        for(auto & [pid, P] : m_procs2)
        {
            kill(pid);
        }

        // and execute the default queue once to clean up
        // everything that was left over. All TRAPS should
        // be executed here if they weren'
        taskQueueExecute(DEFAULT_QUEUE);

        return m_procs2.size();
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
        assert(args.args.size() > 0);
        // Try to find the name of the function to run
        auto it = m_funcs.find(args.args[0]);
        if(it ==  m_funcs.end())
            return invalid_pid;

        //auto & exec_args = args;

        if(!args.in)
        {
            args.in = make_stream();
            //exec_args.in->close();
        }

        // Copies all the arguments into $0 $1 $2 environment
        // variables
        {
            int i=0;
            for(auto & a : args.args)
            {
                args.env[std::to_string(i)] = a;
                ++i;
            }
        }

        if(m_preExec)
            m_preExec(args);

        if(!args.out) args.out = make_stream();
        if(!args.in) args.in = make_stream();

        auto proc_control = std::make_shared<ProcessControl>();
        proc_control->args = args.args;
        proc_control->in   = args.in;
        proc_control->out  = args.out;
        proc_control->env  = args.env;
        proc_control->queue_name= args.queue;


        // If there is a valid parent, then copy all
        // the exported variables from the parent into the
        // new process's environment
        if(parent != invalid_pid)
        {
            auto & exported = PROC_AT(parent)->control->exported;
            auto & parent_env= PROC_AT(parent)->control->env;

            for(auto & [var, exp] : exported)
            {
                if(parent_env.count(var) && proc_control->env.count(var) == 0)
                {
                    proc_control->env[var] = parent_env[var];
                }
            }
        }

        // run the function, it is a coroutine:
        // it will return a task
        auto T = it->second(proc_control);

        auto pid = registerProcess(std::move(T), std::move(proc_control), parent);
        getProcessControl(pid)->chdir("/");

        return pid;
    }

    std::vector<pid_type> runPipeline(std::vector<Exec> E, pid_type parent = invalid_pid)
    {
        if(E.size())
        {
            if(!E.front().in)
                E.front().in = make_stream();
            if(!E.back().out)
                E.back().out = make_stream();
        }

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

    /**
     * @brief registerProcess
     * @param t
     * @param arg
     * @param parent
     * @return
     *
     * Register the coroutine as a valid process within the system. A PID will be
     * generated for it
     */
    pid_type registerProcess(task_type && t, e_type arg, pid_type parent = invalid_pid)
    {
        auto _pid = _pid_count++;
        if(arg == nullptr)
            arg = std::make_shared<ProcessControl>();

        auto handle = t.get_handle();

        auto _t_p = std::make_shared<Process>(arg, std::move(t));
        auto & _t = *_t_p;
        _t.parent = parent;
        arg->pid = _pid;
        arg->system = this;

        if(parent != invalid_pid)
        {
            PROC_AT(parent)->child_processes.push_back(_pid);
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
                for(auto c : this->PROC_AT(aa->pid)->child_processes)
                {
                    this->signal(c, s);
                }
            }
        };
        auto Proc = m_procs2.emplace(_pid, _t_p);

        // Create custom awaiter that will
        // be placed in the main thread pool
        DEBUG_SYSTEM("  Process Registered. PID {}  PARENT: {} : {}", _pid, parent, join(arg->args) );
        Proc.first->second->initialAwaiter = Awaiter(_pid, this, [](Awaiter*){return true;}, arg->queue_name);
        Proc.first->second->initialAwaiter.handle_ = handle;
        Proc.first->second->initialAwaiter.await_suspend(handle);

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
        return !it->second->is_complete;
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
            auto & proc = *PROC_AT(pid);
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
            auto & proc = *PROC_AT(pid);
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
            return {PROC_AT(pid)->control->in, PROC_AT(pid)->control->out};
        }
        return {};
    }

    size_t process_count() const
    {
        return m_procs2.size();
    }
    /**
     * @brief taskQueueExecute
     * @param queue_name
     * @param maxComputeTime
     * @param maxIter
     * @return
     *
     * Execute all the tasks on a particular queue.
     *
     * Keep processing the queue until the maxComputeTime has elapsed or maxIterations
     * has been reached.
     */
    size_t taskQueueExecute(std::string const & queue_name = DEFAULT_QUEUE, std::chrono::milliseconds maxComputeTime=std::chrono::milliseconds(15), size_t maxIter = 1)
    {
        auto T0 = std::chrono::system_clock::now();

        while(maxIter > 0 )
        {
            maxIter--;
            // Execute all the processes in order of their PID
            //
            // Nothing is removed from the container until all
            // of the objects have been processed
            auto & POP_Q  = m_awaiters.at(queue_name).get();
            auto & PUSH_Q = m_awaiters.at(queue_name).get2();
            {
                m_awaiters.at(queue_name).swap();
            }

            std::pair<Awaiter*, std::shared_ptr<Process> > a;

            // Process everything on the queue
            // New tasks will not be added to this queue
            // because of the double buffering
            //DEBUG_SYSTEM("\n\nExecuting {}.  Total Size: {}", queue_name, POP_Q.size_approx());
            while(_processQueue(POP_Q, PUSH_Q,  queue_name))
                ;
            //DEBUG_TRACE("{} Finished Total size: {}", queue_name, POP_Q.size_approx());
            if(queue_name != DEFAULT_QUEUE)
                return PUSH_Q.size_approx() + POP_Q.size_approx();

            // Remove any processes that:
            //   1. whose task has completed
            //   2. who is force terminated
            //
            auto _end = m_procs2.end();
            for(auto it = m_procs2.begin(); it!=_end;)
            {
                auto & coro = *it->second;

                if(coro.task.done() && !coro.is_complete)
                {
                    auto exit_code = coro.task();
                    coro.is_complete = true;
                    *coro.exit_code = !coro.force_terminate ? exit_code : -1;
                    coro.should_remove = true;
                    coro.force_terminate = true;
                }

                // did someone call kill on the PID?
                if(coro.force_terminate)
                {
                    it->second->control->queue_name = queue_name;
                    _finalizePID(it->first);
                }

                if(coro.should_remove)
                {
                    DEBUG_SYSTEM("  Removing PID: {}: {}", coro.control->pid, join(coro.control->args));
                    it = m_procs2.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            if(std::chrono::system_clock::now()-T0 > maxComputeTime)
                break;
        }

        return m_procs2.size();
    }

    void taskQueueCreate(std::string name)
    {
        m_awaiters[name];
    }

    bool taskQueueExists(std::string const & name) const
    {
        return m_awaiters.count(name) == 1;
    }
    size_t taskQueueSize(std::string const & name) const
    {
        return m_awaiters.at(name).m_Q1.size_approx()
               + m_awaiters.at(name).m_Q2.size_approx();
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
            ret = taskQueueExecute();
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
     * You should call this function right after you execute
     * a spawnProcess call, but before the process executes
     *
     * auto p = sys.spawnProcess({"sleep", "10"});
     * auto _exit = sys.getProcessExitCode(p);
     *
     */
    std::shared_ptr<exit_code_type> getProcessExitCode(pid_type p) const
    {
        if(auto it = m_procs2.find(p); it!=m_procs2.end())
        {
            return it->second->exit_code;
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
        return PROC_AT(pid)->control;
    }

    pid_type getParentProcess(pid_type pid)
    {
        return PROC_AT(pid)->parent;
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
        Process(std::shared_ptr<ProcessControl> ctrl, task_type && t) : control(ctrl), task(std::move(t))
        {
        }
        std::shared_ptr<ProcessControl> control;
        task_type                       task;

        bool is_complete = false;
        std::shared_ptr<exit_code_type  > exit_code = std::make_shared<exit_code_type>(-1);
        std::function<void(int)>          signal = {};

        // This is where the current signal is
        int32_t lastSignal = 0;

        // flag indicating whether the process has been signaled
        bool has_been_signaled = false;

        // flag used to indicate that the process should terminate
        // without cleanup.
        bool force_terminate = false;

        // Process has been finalized and is ready to be
        // removed from the scheduler
        bool should_remove = false;

        pid_type                        parent = invalid_pid;
        std::vector<pid_type>           child_processes = {};
        Awaiter initialAwaiter = {};
    };


protected:
    std::map<std::string, std::function< task_type(e_type) >> m_funcs;
    std::map<pid_type, std::shared_ptr<Process> >             m_procs2;

    using awaiter_queue_type = moodycamel::ConcurrentQueue<std::pair<Awaiter*, std::shared_ptr<Process> > >;

    template<typename T>
    struct AwaiterQueue_T
    {
        using value_type = T;
        using queue_type = moodycamel::ConcurrentQueue<value_type>;

        queue_type & get()
        {
            return m_swap ? m_Q2 : m_Q1;
        }
        queue_type & get2()
        {
            return !m_swap ? m_Q2 : m_Q1;
        }

        void swap()
        {
            m_swap = !m_swap;
        }

        inline bool enqueue(value_type const & item)
        {
            return get().enqueue(item);
        }
        inline bool enqueue(value_type && item)
        {
            return get().enqueue(std::move(item));
        }
        inline bool try_dequeue(value_type & item)
        {
            return get().try_dequeue(item);
        }

        bool m_swap = false;
        queue_type m_Q1;
        queue_type m_Q2;
    };

    std::map<std::string,  AwaiterQueue_T<std::pair<Awaiter*, std::shared_ptr<Process> >> > m_awaiters;

    pid_type _pid_count=1;

    void setDefaultFunctions()
    {
#define HANDLE_AWAIT_INT_TERM(returned_signal, CTRL)\
        switch(returned_signal)\
            {\
                case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT: { co_return static_cast<int>(PseudoNix::exit_interrupt);}\
                case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}\
                    default: break;\
            }

#define HANDLE_AWAIT_BREAK_ON_SIGNAL(returned_signal, CTRL)\
            auto returned_signal_value = returned_signal; \
            if(returned_signal_value == PseudoNix::AwaiterResult::SIGNAL_INTERRUPT) { break;}\
            if(returned_signal_value == PseudoNix::AwaiterResult::SIGNAL_TERMINATE) { break;}

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
            auto & ENV = control->env; (void)ENV; \
            auto & EXPORTED = control->exported; (void)EXPORTED; \
            auto const & QUEUE = control->queue_name; (void)QUEUE; \
            auto const & CWD = control->cwd; (void)CWD;\
            auto const PARENT_SHELL_PID = ENV.count("SHELL_PID") ? static_cast<PseudoNix::System::pid_type>(std::stoul(ENV["SHELL_PID"])) : PseudoNix::invalid_pid; (void)PARENT_SHELL_PID;\
            auto const & LAST_SIGNAL = SYSTEM.PROC_AT(PID)->lastSignal; (void)LAST_SIGNAL;\
            auto SHELL_PROC = PARENT_SHELL_PID != PseudoNix::invalid_pid ? SYSTEM.getProcessControl(PARENT_SHELL_PID) : nullptr; (void)SHELL_PROC



#define HANDLE_PATH(CWD, path)\
        {\
            if(path.is_relative())\
                path = CWD / path;\
            path = path.lexically_normal();\
        }

        std::shared_ptr< std::map<std::string, std::string>> funcDescs = std::make_shared< std::map<std::string, std::string> >();
        #define DEF_FUNC_HELP(A, help) \
        (*funcDescs)[A] = help;\
            m_funcs[A] = [](e_type ctrl) -> task_type

        #define DEF_FUNC(A) DEF_FUNC_HELP(A, "")

        DEF_FUNC_HELP("false", "Returns with exit code 1")
        {
            (void)ctrl;
            co_return 1;
        };

        DEF_FUNC_HELP("true", "Returns with exit code 0")
        {
            (void)ctrl;
            co_return 0;
        };

        (*funcDescs)["help"] = "Shows the list of commands";
        m_funcs["help"] = [funcDescs](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);

            //
            // help set <proc> <desc>
            //
            if(ARGS.size() == 4 && ARGS[1] == "set")
            {
                (*funcDescs)[ARGS[2]] = ARGS[3];
                co_return 0;
            }
            COUT << "List of commands:\n\n";
            for(auto & f : ctrl->system->m_funcs)
            {
                COUT << std::format("{:15}: {:15}\n", f.first, (*funcDescs)[f.first]);
            }
            co_return 0;
        };
        DEF_FUNC_HELP("env", "Prints out all environment variables, or sets environment variables for other processes")
        {
            PSEUDONIX_PROC_START(ctrl);

            for(auto & [var, val] : ENV)
            {
                COUT << std::format("{}={}\n", var,val);
            }
            co_return 0;
        };
        DEF_FUNC_HELP("echo", "Prints arguments to standard output")
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
        DEF_FUNC_HELP("yes", "Keeps printing y to stdout until interrupted")
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
        DEF_FUNC_HELP("sleep", "Pauses for NUMBER seconds")
        {
            PSEUDONIX_PROC_START(ctrl);

            std::string output;
            if(ARGS.size() < 2)
                co_return 1;
            float t = 0.0f;
            to_number(ARGS[1], t);
            t = std::max(0.0f, t);
            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(std::chrono::milliseconds( static_cast<uint64_t>(t*1000))), ctrl);

            co_return 0;
        };

        (*funcDescs)["uptime"] = "Number of milliseconds since started";
        m_funcs["uptime"] = [T0=std::chrono::system_clock::now()](e_type ctrl) -> task_type
        {
            PSEUDONIX_PROC_START(ctrl);
            COUT << std::format("{}\n", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-T0).count());
            co_return 0;
        };

        DEF_FUNC_HELP("rev", "Reverses the input")
        {
            PSEUDONIX_PROC_START(ctrl);

            std::string output;
            bool quit = false;
            while(!quit)
            {
                if(AwaiterResult::SUCCESS == co_await ctrl->await_read_line(ctrl->in, output))
                {
                    std::reverse(output.begin(), output.end());
                    *ctrl->out << std::format("{}\n", output);
                    output.clear();
                }
                else
                {
                    break;
                }
            }
            if(!output.empty())
            {
                std::reverse(output.begin(), output.end());
                *ctrl->out << std::format("{}\n", output);
            }
            co_return 0;
        };

        DEF_FUNC_HELP("wc", "Counts the number of characters")
        {
            PSEUDONIX_PROC_START(ctrl);

            uint32_t i=0;

            bool quit = false;
            while(!quit)
            {
                char c;
                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_has_data(ctrl->in), ctrl);

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

        DEF_FUNC_HELP("args", "Prints out information about the arguments")
        {
            PSEUDONIX_PROC_START(ctrl);

            uint32_t i=0;
            for(auto & arg : ARGS)
            {
                COUT << std::format("[{:2}] {}\n",i, arg);
                ++i;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("ps", "Shows the current process list")
        {
            PSEUDONIX_PROC_START(ctrl);

            COUT << std::format("{:<8} {:<10} {}\n", "PID", "QUEUE", "CMD");
            for(auto & [pid, P] : SYSTEM.m_procs2)
            {
                COUT<< std::format("{:<8} {:<10} {}\n", pid, P->control->queue_name, join(P->control->args));
            }

            co_return 0;
        };

        DEF_FUNC_HELP("kill", "Terminate a process")
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() < 2)
                co_return 1;

            pid_type pid = 0;
            if(!to_number(ARGS[1], pid))
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

        DEF_FUNC_HELP("signal", "Send a signal to a process")
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() < 3)
                co_return 1;

            pid_type pid = 0;
            int sig=2;

            if(!to_number(ARGS[1], pid))
            {
                COUT << std::format("Arg 1 must be a Process ID. Recieved {}\n", ARGS[1]);
                co_return 1;
            }


            if(!to_number(ARGS[2], sig))
            {
                COUT << std::format("Arg 2 must be a integer signal code. Recieved {}\n", ARGS[1]);
                co_return 1;
            }


            if(!SYSTEM.signal(pid, sig))
            {
                COUT << std::format("Could not find process ID: {}\n", pid);
                co_return 0;
            }
            co_return 1;
        };

        DEF_FUNC_HELP("io_info", "Shows IO pointers")
        {
            PSEUDONIX_PROC_START(ctrl);

            for(auto & [pid, proc] : SYSTEM.m_procs2)
            {
                COUT << std::format("{}[{}]->{}->{}[{}]\n", static_cast<void*>(proc->control->in.get()), proc->control->in.use_count(), proc->control->args[0], static_cast<void*>(proc->control->out.get()), proc->control->out.use_count() );
            }
            co_return 0;
        };

        DEF_FUNC_HELP("to_std_cout", "Pipes process output to standard output")
        {
            PSEUDONIX_PROC_START(ctrl);
            std::string s;
            while(!CIN.eof())
            {
                if(AwaiterResult::SUCCESS == co_await ctrl->await_read_line(ctrl->in, s))
                {
                    std::cout << s << std::endl;
                    s.clear();
                }
                else
                {
                    break;
                }
            }
            co_return 0;
        };


        //========================
        // Functions for shells
        //========================
        DEF_FUNC_HELP("exit", "Exits the shell")
        {
            PSEUDONIX_PROC_START(ctrl);

            // Not running in a shell
            if(!SHELL_PROC)
                co_return 0;

            // the shell process will
            // look at this variable to determine
            // when to quit
            SHELL_PROC->env["EXIT_SHELL"] = "1";

            int32_t ret_value = 0;
            if( ARGS.size() > 1)
            {
                to_number(ARGS[1], ret_value);
            }
            co_return std::move(ret_value);
        };

        DEF_FUNC("")
        {
            PSEUDONIX_PROC_START(ctrl);
            // Not running in a shell
            if(!SHELL_PROC)
               co_return 0;
            // This function will be called, if we set environment variables
            // but didn't call an actual function, eg:
            //     VAR=value VAR2=value2
            for(auto & [var,val] : ENV)
            {
               SHELL_PROC->env[var] = val;
            }
            co_return 0;
        };

        DEF_FUNC_HELP("export", "Exports environment variables to new processes")
        {
           PSEUDONIX_PROC_START(ctrl);

           // Not running in a shell
           if(!SHELL_PROC)
               co_return 0;
           for(size_t i=1;i<ARGS.size();i++)
           {
               auto [var, val] = splitVar(ARGS[i]);
               if(!var.empty() && !val.empty())
               {
                   // if the arg looked like: VAR=VAL
                   // then set the variable as well as
                   // export it
                   SHELL_PROC->exported[std::string(var)] = true;
                   SHELL_PROC->env[std::string(var)] = val;
               }
               else
               {
                   // just export the variable
                   SHELL_PROC->exported[std::string(ARGS[i])] = true;
               }
           }
           co_return 0;
        };

        DEF_FUNC_HELP("exported", "Prints exported environment variables")
        {
           PSEUDONIX_PROC_START(ctrl);
           // Not running in a shell
           if(!SHELL_PROC)
               co_return 0;
           for(auto & x : SHELL_PROC->exported)
           {
               COUT << x.first << '\n';
           }
           co_return 0;
        };

        DEF_FUNC_HELP("cd", "Changes the current working directory")
        {
           PSEUDONIX_PROC_START(ctrl);

           // Not running in a shell
           if(!SHELL_PROC)
               co_return 0;

           if(ARGS.size() == 1)
           {
               SHELL_PROC->chdir("/");
               co_return 0;
           }

           if(ARGS[1] == "-")
           {
               SHELL_PROC->chdir(SHELL_PROC->env["OLDPWD"]);
               co_return 0;
           }

           System::path_type p = ARGS[1];
           if(p.is_relative())
               p = SHELL_PROC->cwd / p;

           p = p.lexically_normal();
           if(!SYSTEM.exists(p))
           {
               COUT << std::format("cd: {}: No such file or directory\n", ARGS[1]);
               co_return 1;
           }

           if(SHELL_PROC->chdir(p))
               co_return 0;

           COUT << std::format("Unknown error\n");
           co_return 1;

        };

        DEF_FUNC_HELP("spawn", "Spawns N instances of the same process")
        {
            // Executes a command N times.
            //
            //  Usage: spawn 10 echo hello world
            //
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() < 3)
            {
                COUT << std::format("Error: \n\n  spawn <count> cmd <args...>\n");
                co_return 1;
            }
            size_t count = 0;

            if( !to_number(ARGS[1], count))
            {
                COUT << std::format("Error: Arg 1 must be a postive number");
            }
            count = std::clamp<size_t>(count, 0u, 1000u);

            while(count--)
            {
                auto E = System::parseArguments( std::vector(ARGS.begin()+2, ARGS.end()) );
                E.out = ctrl->out;
                SYSTEM.runRawCommand(E);
            }

            co_return 0;
        };

        DEF_FUNC_HELP("bgrunner", "Spawn a background thread to process a Task Queue")
        {
            // Executes a Task Queue in a background thread
            // You can call this function on the same Task Queue
            // multiple times to spawn multiple threads on the queue
            // effectively creating a threadpool
            //
            PSEUDONIX_PROC_START(ctrl);
            #if defined __EMSCRIPTEN__
            COUT << "This command does not work on Emscripten at the moment.\n";
            co_return 1;
            #endif
            std::string s;

            std::string TASK_QUEUE = ARGS.size() < 2 ? std::string("THREADPOOL") : ARGS[1];
            std::atomic<bool> stop_token = false;

            std::binary_semaphore _semaphore(0);

            if(!SYSTEM.taskQueueExists(TASK_QUEUE))
            {
                COUT << std::format("{}: Task queue, {}, does not exist", ARGS[0], TASK_QUEUE);
                co_return 1;
            }

            if(TASK_QUEUE == System::DEFAULT_QUEUE)
            {
                COUT << std::format("{}: Cannot run background thread on {} queue.\n", ARGS[0], TASK_QUEUE);
                co_return 1;
            }
            std::thread worker([sys=ctrl->system, TASK_QUEUE, &stop_token, &_semaphore]()
            {
                while (true)
                {
                    if(sys->m_awaiters.count(TASK_QUEUE) == 0)
                    {
                        DEBUG_ERROR("Cannot find {}. Exiting", TASK_QUEUE);
                        break;
                    }
                    auto & TQ = sys->m_awaiters.at(TASK_QUEUE);
                    auto & Q = TQ.get();

                    auto is_empty = !sys->_processQueue(Q, Q, TASK_QUEUE);
                    if(is_empty)
                    {
                        DEBUG_TRACE("No Tasks. Sleeping: {}", std::this_thread::get_id());
                        _semaphore.acquire();
                        DEBUG_TRACE("Woke up: {}", std::this_thread::get_id());
                    }
                    if(stop_token)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                DEBUG_INFO("Thread exit");
            });

            PSEUDONIX_TRAP {
                DEBUG_TRACE("TRAPPED: {}", QUEUE);
                // set the stop token so the thread will exit
                // its main loop
                stop_token = true;
                // trigger the semaphore so that the thread
                // will wake up and quit
                _semaphore.release();
                worker.join();
            };

            while(true)
            {
                auto & TQ = SYSTEM.m_awaiters.at(TASK_QUEUE);
                // Wake the thread up if there are items
                // in the queue
                if(TQ.get().size_approx() > 0)
                    _semaphore.release();
                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
            }

            co_return 0;
        };

        DEF_FUNC_HELP("queue", "Create/List/Destroy task queues")
        {
            // Lists all the queues and the number of tasks
            // currently in the queue
            PSEUDONIX_PROC_START(ctrl);
            if(ARGS.size() == 1)
            {
                COUT << std::format("Error in arguments:\n\n");
                COUT << std::format("Usage: {} [list|create|destroy] <queue name>\n", ARGS[0]);
                co_return 1;
            }
            if( ARGS[1] == "list")
            {
                for(auto & a : SYSTEM.m_awaiters)
                {
                    COUT << std::format("{} {}\n", a.first, a.second.get().size_approx());
                }
                co_return 0;
            }
            if( ARGS[1] == "create" )
            {
                if(ARGS.size() != 3)
                {
                    COUT << std::format("Requires a name for the queue\n");
                    co_return 1;
                }
                SYSTEM.m_awaiters[ARGS[2]];
                co_return 0;
            }
            if( ARGS[1] == "destroy" )
            {
                if(ARGS.size() != 3)
                {
                    COUT << std::format("Requires a name for the queue\n");
                    co_return 1;
                }
                if(ARGS[2] == "HOME")
                {
                    COUT << std::format("Error: Cannot destroy the HOME queue\n");
                    co_return 1;
                }
                SYSTEM.m_awaiters.erase(ARGS[2]);
                co_return 0;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("queueHopper", "Example process that hops to different task queues")
        {
            //
            // An example process which executes part of its
            // code on the MAIN queue and part of the code on
            // a different QUEUE
            //
            PSEUDONIX_PROC_START(ctrl);
            using namespace std::chrono_literals;
            if( ARGS.size() < 2)
            {
                COUT << std::format("Requires a Task Queue name\n\n   queueHopper <queue name>");
                co_return 1;
            }
            std::string TASK_QUEUE = ARGS[1];

            if(!SYSTEM.taskQueueExists(TASK_QUEUE))
            {
                COUT << std::format("Task queue, {}, does not exist. The Task Queue needs to be created using 'queue create <name>' ", TASK_QUEUE);
                co_return 1;
            }

            PSEUDONIX_TRAP {
                COUT << std::format("Trap on {} queue\n", QUEUE);
            };

            {
                auto _lock = COUT.lock();
                COUT << std::format("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
            }

            // the QUEUE variable defined by PSEUDONIX_PROC_START(ctrl)
            // tells you what queue this process is being executed on
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(250ms, DEFAULT_QUEUE), ctrl);

            {
                auto _lock = COUT.lock();
                COUT << std::format("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
            }

            for(int i=0;i<10;i++)
            {
                // wait for 1 second and then resume on a different Task Queue
                // Specific task queues are executed at a specific time
                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(250ms, TASK_QUEUE), ctrl);

                {
                    // Only one thread can read/write to the pipes at a time
                    // it is quite likely that COUT is being shared by multiple
                    // processes. So we ensure that we lock access
                    // to it so that it doesn't cause any race conditions
                    auto _lock = COUT.lock();
                    COUT << std::format("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
                }

                HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(500ms, DEFAULT_QUEUE), ctrl);

                {
                    auto _lock = COUT.lock();
                    COUT << std::format("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
                }
            }

            // finally make sure we are on the main queue
            // when we exit so that the TRAP function will be executed
            // on that
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(500ms, DEFAULT_QUEUE), ctrl);
            {
                auto _lock = COUT.lock();
                COUT << std::format("Last On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
            }

            co_return 0;
        };


        DEF_FUNC_HELP("pwd", "Prints the current working directory")
        {
            PSEUDONIX_PROC_START(ctrl);
            COUT << std::format("{}\n", ctrl->cwd.generic_string());
            co_return 0;
        };

#define FS_PRINT_ERROR(_error) \
        switch(_error)\
        {\
        case FSResult::False:\
        case FSResult::True:\
            break;\
        case FSResult::ErrorNotEmpty: COUT << std::format("Location is not empty"); break;\
        case FSResult::ErrorReadOnly: COUT << std::format("Location is read-only"); break;\
        case FSResult::ErrorNotFile: COUT << std::format("Location is not a file"); break;\
        case FSResult::ErrorNotDirectory: COUT << std::format("Location is not a directory"); break;\
        case FSResult::ErrorDoesNotExist: COUT << std::format("File or folder does not exists"); break;\
        case FSResult::ErrorExists: COUT << std::format("File or folder already exists"); break;\
        case FSResult::ErrorParentDoesNotExist: COUT << std::format("Unknown Error"); break;\
        case FSResult::ErrorIsMounted: COUT << std::format("Location is a mounted file/directory"); break;\
        case FSResult::UnknownError: COUT << std::format("Unknown Error"); break;\
        }


        DEF_FUNC_HELP("ls", "Lists files and directories")
        {
            PSEUDONIX_PROC_START(ctrl);
            path_type path = CWD;

            if(ARGS.size() >= 2)
            {
                path_type p = ARGS[1];
                HANDLE_PATH(CWD, p)
                path = p;
            }

            assert(path.has_root_directory());

            for(auto u : SYSTEM.list_dir(path))
            {
                COUT << std::format("{}\n", u.generic_string());
            }

            co_return 0;
        };

        DEF_FUNC_HELP("mkdir", "Create directories")
        {
            PSEUDONIX_PROC_START(ctrl);
            path_type path = "/";
            if(ARGS.size() >= 2)
            {
                path = ARGS[1];
                HANDLE_PATH(CWD, path);

                auto res = SYSTEM.mkdir(path);

                FS_PRINT_ERROR(res);
            }
            else
            {
                COUT << std::format("mkdir: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };


        DEF_FUNC_HELP("rm", "Removes files and directories")
        {
            PSEUDONIX_PROC_START(ctrl);
            path_type path = "/";
            if(ARGS.size() >= 2)
            {
                for(size_t i=1; i < ARGS.size();i++)
                {
                    path = ARGS[i];
                    HANDLE_PATH(CWD, path);
                    if(!SYSTEM.remove(path))
                    {
                        COUT << std::format("Error deleting file: {}", path.generic_string());
                        co_return 1;
                    }
                }
            }
            else
            {
                COUT << std::format("touch: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("touch", "Create files")
        {
            PSEUDONIX_PROC_START(ctrl);
            path_type path = "/";
            if(ARGS.size() >= 2)
            {
                for(size_t i=1; i < ARGS.size();i++)
                {
                    path = ARGS[i];
                    HANDLE_PATH(CWD, path);
                    auto res = SYSTEM.mkfile(path);
                    FS_PRINT_ERROR(res);
                }
            }
            else
            {
                COUT << std::format("touch: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("cp", "Copies files and directories")
        {
            PSEUDONIX_PROC_START(ctrl);
            if(ARGS.size() >= 3)
            {
                path_type cpy_to = ARGS.back();
                HANDLE_PATH(CWD, cpy_to);

                for(size_t i=1; i<ARGS.size()-1;i++)
                {
                    path_type path = ARGS[i];
                    _clean(path);
                    HANDLE_PATH(CWD, path);
                    SYSTEM.copy(path, cpy_to);
                }
            }
            else
            {
                COUT << std::format("touch: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };

#if 1
        (*funcDescs)["mount"] = "Mounts host filesystems inside the VFS";
        m_funcs["mount"] = [](e_type ctrl) -> task_type
        {
            //
            // mount [host/archive] <src> <mnt point>
            //

            PSEUDONIX_PROC_START(ctrl);

            int ret_value;

            if(ARGS.size() == 1)
            {
                // list all mounts
                for(auto c : SYSTEM.list_nodes_recursive("/"))
                {
                    auto [srcMnt, srcRem ] = SYSTEM.find_last_valid_virtual_node(c);
                    if(auto d = std::dynamic_pointer_cast<FSNodeDir>(srcMnt))
                    {
                        if(d->mount)
                        {
                            COUT << std::format("{} on {}\n", d->mount->get_info(), c.generic_string());
                        }
                    }
                }
            }
            else if(ARGS.size() == 4)
            {
                auto TYPE = ARGS[1];
                System::path_type SRC  = ARGS[2];
                System::path_type DST  = ARGS[3];
                HANDLE_PATH(CWD, DST);
                HANDLE_PATH(CWD, SRC);

                std::vector<std::string> mount_args = { TYPE, "mount"};
                for(size_t i=2;i<ARGS.size();i++)
                {
                    mount_args.push_back(ARGS[i]);
                }
                auto E = System::parseArguments(mount_args);
                E.in = ctrl->in;
                E.out = ctrl->out;

                auto s_pid = ctrl->executeSubProcess(E);
                HANDLE_AWAIT_TERM( co_await ctrl->await_finished(s_pid), ctrl);

                if(!to_number(ENV["?"], ret_value))
                {
                    co_return 0;
                }
            }

            co_return std::move(ret_value);
        };

        DEF_FUNC_HELP("umount", "Unmounts a host filesystem")
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() == 2)
            {
                path_type p = ARGS[1];
                if(!p.has_root_directory())
                    p = CWD / p;
                auto res = SYSTEM.unmount(p);
                FS_PRINT_ERROR(res);
                co_return 0;
            }
            COUT << std::format("Unknown error\nUsage:\n umount <mount point>\n");

            co_return 1;
        };
#endif
        DEF_FUNC_HELP("test", "Test file types and compares values")
        {
            // very simple implemntation of the "test" function in linux
            // mostly used in if statements in shell scripts
            PSEUDONIX_PROC_START(ctrl);
            if(ARGS.size() == 1)
                co_return 0;

            auto _args = ARGS;
            // test [flag] [file_path]
            bool negate=false;
            auto _cmp = [&negate](bool f)
            {
                return negate ? f : (!f);
            };

            while(_args[1] == "!")
            {
                negate = !negate;
                _args.erase(_args.begin()+1);
            }

            if(_args.size() == 3)
            {
                auto const & flag = _args[1];
                path_type path = _args[2];
                if(!path.has_root_directory())
                    path = CWD / path;

                auto t = SYSTEM.getType(path);
                if(flag == "-f")
                {
                    // note: 0 == true and 1 == false in
                    // a shell
                    co_return _cmp(t == NodeType::MemFile || t == NodeType::MountFile);
                }
                else if(flag == "-d")
                {
                    co_return _cmp(t == NodeType::MemDir || t == NodeType::MountDir);
                }
                else if (flag == "-e")
                {
                    co_return _cmp(t != NodeType::NoExist);
                }
            }
            else if (_args.size() == 4)
            {
                auto const & left = _args[1];
                auto const & op = _args[2];
                auto const & right = _args[3];
                if(op == "=")
                {
                    co_return _cmp(left==right);
                }
                else if(op == "!=")
                {
                    co_return _cmp(left!=right);
                }
                else
                {
                    int32_t left_int=0;
                    int32_t right_int=0;
                    if(!to_number(left, left_int))
                    {
                        COUT << std::format("test: {}: integer expression expected\n", left);
                        co_return 2;
                    }
                    if(!to_number(right, right_int))
                    {
                        COUT << std::format("test: {}: integer expression expected\n", right);
                        co_return 2;
                    }
                    if( op == "-eq")
                        co_return _cmp(left_int == right_int);
                    if( op == "-le")
                        co_return _cmp(left_int <= right_int);
                    if( op == "-lt")
                        co_return _cmp(left_int < right_int);
                    if( op == "-ge")
                        co_return _cmp(left_int >= right_int);
                    if( op == "-gt")
                        co_return _cmp(left_int > right_int);
                }
                // test  AA == BB
            }
            co_return 0;
        };

        DEF_FUNC_HELP("cat", "Concatenates files to standard output")
        {
            PSEUDONIX_PROC_START(ctrl);

            if(ARGS.size() == 2)
            {
                path_type path = ARGS[1];
                if(!path.has_root_directory())
                    path = CWD / path;

                switch(SYSTEM.getType(path))
                {
                    case NodeType::MemFile:
                    case NodeType::MountFile:
                    {
                        auto file = SYSTEM.openRead(path);
                        if (!file) {
                            co_return 1;
                        }
                        auto T0 = std::chrono::system_clock::now();
                        std::string line;
                        while(true)
                        {
                            while(!file.eof() && (std::chrono::system_clock::now()-T0 < std::chrono::microseconds(1000)) )
                            {
                                std::getline(file, line);
                                COUT << line;
                                COUT << "\n";
                            }
                            if(file.eof())
                                break;
                            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
                            T0 = std::chrono::system_clock::now();
                        }
                        co_return 0;
                    }
                    default:
                    {
                        COUT << std::format("cat: {}: Not a regular file", ARGS[1]);
                        co_return 1;
                    }
                }

                COUT << "\n";
                co_return 0;
            }
            co_return 1;
        };

        DEF_FUNC_HELP("blocking_sleep", "Like [sleep], but will block. For demo purposes only.")
        {
            PSEUDONIX_PROC_START(ctrl);

            std::string output;
            if(ARGS.size() < 2)
                co_return 1;
            float t = 0.0f;

            to_number(ARGS[1], t);

            t = std::max(0.0f, t);
            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            std::this_thread::sleep_for(std::chrono::milliseconds( static_cast<uint64_t>(t*1000)));

            co_return 0;
        };
        #undef DEF_FUNC
    }

    void handleAwaiter(Awaiter *a)
    {
        auto pid = a->get_pid();
        auto proc = PROC_AT(pid);

        // the queue must have been created prior to
        // adding tasks
        auto it = m_awaiters.find(a->m_queueName);
        if(it != m_awaiters.end())
        {
            it->second.enqueue({a,proc});
        }
        else
        {
            DEBUG_ERROR("{} not found. Adding to MAIN", a->m_queueName);
            m_awaiters.at(DEFAULT_QUEUE).enqueue({a,proc});
        }
    }

    // End the process and clean up anything
    // regardless of whether it was complete
    // Does not remove the pid from the process list
    void _finalizePID(pid_type p)
    {
        auto & coro = *PROC_AT(p);
        coro.control->queue_name = DEFAULT_QUEUE;
        if( coro.task.valid() )
        {
            if(coro.task.destroy())
            {
                DEBUG_TRACE("Coroutine frame destroyed: {}", join(coro.control->args));
            }
        }
        // the output stream should have its
        // EOF set so that any processes
        // reading from it will know to close
        coro.control->out->set_eof();

        coro.is_complete = true;

        _detachFromParent(p);

        DEBUG_TRACE("Finalized: {}", join(coro.control->args));
        DEBUG_TRACE("       IN: {}", coro.control->in.use_count());
        DEBUG_TRACE("      OUT: {}", coro.control->out.use_count());

        coro.control->out = {};
        coro.control->in  = {};

        // set the flag so that
        // it will be removed
        coro.should_remove = true;
    }

    void _detachFromParent(pid_type p)
    {
        auto & coro = *PROC_AT(p);
        if(coro.parent != invalid_pid && m_procs2.count(coro.parent))
        {
            auto & cp = PROC_AT(coro.parent)->child_processes;
            cp.erase(std::remove_if(cp.begin(), cp.end(), [p](auto && ch)
                                    {
                                        return ch==p;
                                    }), cp.end());
            coro.parent = invalid_pid;
        }
    }


    /**
     * @brief _processQueue
     * @param POP_Q
     * @param PUSH_Q
     * @param queue_name
     * @return
     *
     * Process a single item on the queue and returns true if it was able to
     * other wise, return false if no items are on the queue
     *
     */
    bool _processQueue(auto & POP_Q, auto & PUSH_Q, std::string queue_name)
    {
        std::pair<Awaiter*, std::shared_ptr<Process> > a;
        auto found = POP_Q.try_dequeue(a);
        if(found)
        {
            if(!a.first->handle_)
                return false;
            // its possible that the process had been forcefully killed
            // and the handle to the coroutine no longer valid. So make sure
            // that we do not resume any of those coroutines
            if(a.second->force_terminate || a.second->is_complete || a.second->should_remove)
                return found;

            assert(!a.second->should_remove);
            if(a.first->await_ready())
            {
                a.second->control->queue_name = queue_name;
                a.second->control->env["QUEUE"] = queue_name;
                a.second->control->env["THREAD_ID"] = std::format("{}", std::this_thread::get_id());
                DEBUG_SYSTEM("  Resuming on QUEUE: {} PID: {} : {}", queue_name, a.second->control->pid, join(a.second->control->args));
                a.first->resume();
            }
            else
            {
                PUSH_Q.enqueue(std::move(a));
            }
        }
        return found;
    }


};


}


#endif

