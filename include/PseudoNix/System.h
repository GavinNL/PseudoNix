#ifndef PSEUDONIX_SYSTEM_H
#define PSEUDONIX_SYSTEM_H

#include <cmath>
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

enum class eSignal
{
    NONE = 0,
    INTERRUPT = 2,
    KILL = 9,
    TERMINATE = 15,
    CONTINUE = 18,
    STOP = 19
};


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
    SIGNAL_INTERRUPT = static_cast<int>(eSignal::INTERRUPT),//sig_interrupt,
    SIGNAL_TERMINATE = static_cast<int>(eSignal::TERMINATE),//sig_terminate,

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
    using clock_type = std::chrono::system_clock;
    using user_id_type = uint32_t;

    std::string DEFAULT_QUEUE = "MAIN";
    clock_type::duration DEFAULT_PROC_TIME = std::chrono::milliseconds(1);

    struct user_t
    {
        std::string name;
    };

    bool userCreate(user_id_type id, std::string name)
    {
        if (m_users.count(id) > 0)
        {
            return false;
        }
        auto &U = m_users[id];
        U.name = name;
        return true;
    }
    bool userDelete(user_id_type id)
    {
        if (id == 0)
            return false;
        return m_users.erase(id) > 0;
    }
    auto &PROC_AT(auto &&key)
    {
        auto it = m_procs2.find(key);
        if(it == m_procs2.end())
        {
            throw std::runtime_error(std::format("Cannot find PID: {}", key));
        }
        return it->second;
    }
    auto const &PROC_AT(auto &&key) const
    {
        auto it = m_procs2.find(key);
        if (it == m_procs2.end())
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
        std::string queue; // = DEFAULT_QUEUE;

        // Custom user variable you use to
        // pass data.
        std::shared_ptr<void> userData1;
        std::shared_ptr<void> userData2;

        Exec(std::vector<std::string> const &_args = {},
             std::map<std::string, std::string> const &_env = {})
            : args(_args)
            , env(_env)
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
                case eSignal::INTERRUPT: m_result = AwaiterResult::SIGNAL_INTERRUPT; m_signal = {}; return true; break;
                case eSignal::TERMINATE: m_result = AwaiterResult::SIGNAL_TERMINATE; m_signal = {}; return true; break;
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
        eSignal * m_signal = nullptr;
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
        std::string queue_name;                        // which queue to run on
        System * system = nullptr;
        path_type cwd = "/";

        std::shared_ptr<void> userData1;
        std::shared_ptr<void> userData2;

        std::chrono::system_clock::time_point last_resume_time = {};

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
        void setSignalHandler(std::function<void(eSignal)> f)
        {
            system->PROC_AT(pid)->signal = f;
        }

        pid_type get_pid() const
        {
            return pid;
        }

        /**
         * @brief await_yield_time
         * @param queue
         * @return 
         * 
         * Yields if the time since last yield is greater than maxComputeTime
         * 
         */
        System::Awaiter await_yield_time(std::chrono::system_clock::duration maxComputeTime,
                                         std::string_view queue = {})
        {
            std::string_view _queue = queue.empty() ? this->queue_name : queue;
            auto ctrl = system->getProcessControl(pid);
            return System::Awaiter{pid,
                                   system,
                                   [ctrl, maxComputeTime](Awaiter *) mutable {
                                       return clock_type::now() - ctrl->last_resume_time
                                              < maxComputeTime;
                                   },
                                   std::string(_queue)};
        }

        /**
         * @brief await_yield
         * @return
         *
         * Yield the current process until the
         * next iteration of the scheduler
         */
        System::Awaiter await_yield(std::string_view queue = {})
        {
            std::string_view _queue = queue.empty() ? this->queue_name : queue;
            return System::Awaiter{pid,
                                   system,
                                   [x = false](Awaiter *) mutable {
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
                                   },
                                   std::string(_queue)};
        }

        /**
         * @brief await_yield_for
         * @param time
         * @return
         *
         * Sleep for an amount of time.
         */
        System::Awaiter await_yield_for(std::chrono::nanoseconds time, std::string_view queue = {})
        {
            std::string_view _queue = queue.empty() ? this->queue_name : queue;
            auto T1 = std::chrono::system_clock::now() + time;
            return System::Awaiter{get_pid(),
                                   system,
                                   [T = T1](Awaiter *) {
                                       return std::chrono::system_clock::now() > T;
                                   },
                                   std::string(_queue)};
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

        pid_type executeSubProcess(std::vector<std::string> const &_args)
        {
            return system->runRawCommand(parseArguments(_args), get_pid());
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

    Generator<pid_type> get_processes() const
    {
        for (auto it = m_procs2.begin(); it != m_procs2.end(); ++it)
        {
            co_yield it->first;
        }
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
            signal(pid, eSignal::KILL);
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
    pid_type runRawCommand(Exec args, pid_type parent = invalid_pid, bool start_suspended = false)
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
        proc_control->queue_name = args.queue.size() == 0 ? DEFAULT_QUEUE : args.queue;
        proc_control->userData1 = args.userData1;
        proc_control->userData2 = args.userData2;

        uint32_t parent_user = 0;

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

            parent_user = getProcessUser(parent);
        }

        // set environment variables
        {
            if (m_users.count(parent_user))
            {
                auto &U = m_users.at(parent_user);
                proc_control->env["USER"] = std::format("{}", U.name);
            }
        }
        proc_control->env["QUEUE"] = proc_control->queue_name;

        // run the function, it is a coroutine:
        // it will return a task
        auto T = it->second(proc_control);

        auto pid = registerProcess(std::move(T), std::move(proc_control), parent);
        getProcessControl(pid)->chdir("/");

        auto & Proc = m_procs2.at(pid);

        Proc->user_id = parent_user;

        if (!start_suspended)
        {
            auto handle = Proc->task.get_handle();
            // Create custom awaiter that will
            // be placed in the main thread pool
            DEBUG_SYSTEM("  Process Registered. PID {}  PARENT: {} : {}", pid, parent, join(arg->args) );
            Proc->initialAwaiter = Awaiter(pid, this, [](Awaiter*){return true;}, Proc->control->queue_name);

            // call the await suspend on the handle
            // so that it will be added to the appropriate queue
            Proc->initialAwaiter.await_suspend(handle);
        }

        return pid;
    }

    std::vector<pid_type> runPipeline(std::vector<Exec> E, pid_type parent = invalid_pid, bool start_suspended = false)
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
            out.push_back(runRawCommand(e,parent,start_suspended));
        }
        return out;
    }

    /**
     * @brief registerProcess
     * @param t
     * @param arg
     * @param parent
     * @return
     *
     * Register the coroutine as a valid process within the system. A PID will be
     * generated for it.
     *
     * The process will be started in a suspended state.
     *
     * You have to manually start it by calling resume(pid)
     */
    pid_type registerProcess(task_type && t, e_type arg, pid_type parent = invalid_pid)
    {
        auto _pid = _pid_count++;
        if(arg == nullptr)
            arg = std::make_shared<ProcessControl>();

        //auto handle = t.get_handle();

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
        _t.signal = [p, this](eSignal s)
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
        Proc.first->second->state = Process::SUSPENDED;
        Proc.first->second->args = arg->args;
        (void)Proc;

        return _pid;
    }

    /**
     * @brief isRunning
     * @param pid
     * @return
     *
     *
     * Checks if a specific pid is running. If a PID is in the
     * suspended state, it is still considered running.
     *
     * The PID is only not-running if it has completed the
     *
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
     * @brief resume
     * @param pid
     * @return
     *
     * Resumes the PID if it is in a SUSPENDED state. This is
     * different from an AWAITING state. A suspended
     * PID does not have an awaiter in a queue.
     *
     * Currently the only way for the PID to be in a SUSPENDED
     * state is if runRawCommand(args, parent, start_suspened=true)
     */
    bool resume(pid_type pid)
    {
        auto & P = PROC_AT(pid);
        if(P->state == Process::SUSPENDED)
        {
            _resume_task_now(P);
            return true;
        }
        return false;
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
    bool signal(pid_type pid, eSignal sigtype)
    {
        if(isRunning(pid))
        {
            auto & proc = *PROC_AT(pid);

            if(sigtype == eSignal::STOP)
            {
                proc.state = Process::SUSPENDING;
                // sig stop
            }
            else if( sigtype == eSignal::CONTINUE)
            {
                // sig cont
                if (proc.state == Process::SUSPENDED || proc.state == Process::SUSPENDING)
                    proc.state = Process::RESUMING;
            }
            else if( sigtype == eSignal::KILL)
            {
                if(isRunning(pid))
                {
                    proc.force_terminate = true;
                    return true;
                }
            }
            else
            {
                if(proc.signal && !proc.has_been_signaled)
                {
                    proc.has_been_signaled = true;
                    proc.lastSignal = sigtype;
                    proc.signal(sigtype);
                    proc.has_been_signaled = false;
                }
            }

            return true;
        }
        return false;
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
        return signal(pid, eSignal::INTERRUPT);
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
        return signal(pid, eSignal::KILL);
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
                signal(pid, eSignal::TERMINATE);
        }
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
            proc.lastSignal = eSignal::NONE;
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
     * @brief defaultQueue
     * @return 
     * 
     * Returns the default queue.
     */
    std::string_view const defaultQueue() const
    {
        //
        return DEFAULT_QUEUE;
    }
    void defaultQueueSet(std::string_view str)
    {
        //
        DEFAULT_QUEUE = str;
    }

    /**
     * @brief defeaultProcessTime
     * 
     * Returns the default maximum process time. You can 
     * change this value by calling setDefaultMaximumProcessTime().
     * 
     * This this value is used in the PN_YIELD macro to determine
     * whether to yield to the next iteration or not
     */
    auto defeaultProcessTime() const
    { //
        return DEFAULT_PROC_TIME;
    }
    void defaultProcessTimeSet(clock_type::duration dur)
    {
        //
        DEFAULT_PROC_TIME = dur;
    }

    size_t taskQueueExecute(std::chrono::milliseconds maxComputeTime = std::chrono::milliseconds(15),
                            size_t maxIter = 1)
    {
        return taskQueueExecute(defaultQueue(), maxComputeTime, maxIter);
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
    size_t taskQueueExecute(std::string_view const queue_name,
                            std::chrono::milliseconds maxComputeTime = std::chrono::milliseconds(15),
                            size_t maxIter = 1)
    {
        auto T0 = std::chrono::system_clock::now();

        std::string q_name = std::string(queue_name);
        while (maxIter > 0)
        {
            maxIter--;
            // Execute all the processes in order of their PID
            //
            // Nothing is removed from the container until all
            // of the objects have been processed
            auto &POP_Q = m_awaiters.at(q_name).get();
            auto &PUSH_Q = m_awaiters.at(q_name).get2();
            {
                m_awaiters.at(q_name).swap();
            }

            // Process everything on the queue
            // New tasks will not be added to this queue
            // because of the double buffering
            //DEBUG_SYSTEM("\n\nExecuting {}.  Total Size: {}", queue_name, POP_Q.size_approx());
            while (_processQueue(POP_Q, PUSH_Q, q_name))
                ;
            //DEBUG_TRACE("{} Finished Total size: {}", queue_name, POP_Q.size_approx());
            if (queue_name != DEFAULT_QUEUE)
                return PUSH_Q.size_approx() + POP_Q.size_approx();

            // Remove any processes that:
            //   1. whose task has completed
            //   2. who is force terminated
            //
            auto _end = m_procs2.end();
            for (auto it = m_procs2.begin(); it != _end;)
            {
                auto &coro = *it->second;

                // did someone call kill on the PID?
                if (coro.force_terminate && coro.state != Process::FINALIZED)
                {
                    it->second->control->queue_name = queue_name;
                    _finalizePID(it->first);
                }

                if (coro.state == Process::FINALIZED)
                {
                    DEBUG_SYSTEM("  Removing PID: {}: {}", coro.control->pid, join(coro.control->args));
                    it = m_procs2.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            if (std::chrono::system_clock::now() - T0 > maxComputeTime)
                break;
        }

        return m_procs2.size();
    }

    void taskQueueCreate(std::string name) { m_awaiters[name]; }

    bool taskQueueExists(std::string const &name) const { return m_awaiters.count(name) == 1; }
    size_t taskQueueSize(std::string const &name) const
    {
        return m_awaiters.at(name).m_Q1.size_approx() + m_awaiters.at(name).m_Q2.size_approx();
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
        auto T1 = std::chrono::high_resolution_clock::now() + d;
        size_t ret = 0;
        size_t i = 0;
        while (true)
        {
            ret = taskQueueExecute();
            if (std::chrono::high_resolution_clock::now() > T1 || i++ > maxIterations)
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
        if (auto it = m_procs2.find(p); it != m_procs2.end())
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

    pid_type getParentProcess(pid_type pid) { return PROC_AT(pid)->parent; }

    uint32_t getProcessUser(pid_type pid) const { return PROC_AT(pid)->user_id; }
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
        auto it = std::find_if(e.args.begin(), e.args.end(), [&e](auto &arg) {
            auto [var, val] = splitVar(arg);
            if (!var.empty())
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
    static std::vector<Exec> genPipeline(std::vector<std::vector<std::string>> array_of_args)
    {
        std::vector<Exec> out;

        for (size_t i = 0; i < array_of_args.size(); i++)
        {
            out.push_back(parseArguments(array_of_args[i]));
            out.back().out = make_stream();
        }
        for (size_t i = 1; i < out.size(); i++)
        {
            out[i].in = out[i - 1].out;
        }
        return out;
    }

    std::function<void(Exec &)> m_preExec;

    struct Process
    {
        enum State {
            UNKNOWN,
            INITIALIZED, // - Task has been initialized
            RUNNING,     // - Task is currently running
            AWAITING,    // - Task is awaiting information before
                         //   it can resume. It currently exists
                         //   on a task queue
            SUSPENDING,  // - Task has been scheduled to suspend
            SUSPENDED,   // - Task has been suspended (ie: not awaiting)
                         //   It must be resumed by calling System::resume(pid)
                         //   The task does NOT EXIST on a task queue.
            RESUMING,    // - The task has been scheduled to resume
            EXITED,      //   task has reached a co_return statement
            FINALIZED    //   Task has been finalize and ready to be
                         //   removed from the system
        };

        Process(std::shared_ptr<ProcessControl> ctrl, task_type &&t)
            : control(ctrl)
            , task(std::move(t))
        {}
        std::shared_ptr<ProcessControl> control;
        task_type task;

        bool is_complete = false;
        std::shared_ptr<exit_code_type> exit_code = std::make_shared<exit_code_type>(-1);
        std::function<void(eSignal)> signal = {};

        // This is where the current signal is
        eSignal lastSignal = eSignal::NONE;

        // flag indicating whether the process has been signaled
        bool has_been_signaled = false;

        // flag used to indicate that the process should terminate
        // without cleanup.
        bool force_terminate = false;

        State state = UNKNOWN;

        uint32_t user_id = 0;

        std::vector<std::string> args;

        std::chrono::system_clock::duration process_time = {};

        pid_type parent = invalid_pid;
        std::vector<pid_type> child_processes = {};
        Awaiter initialAwaiter = {};
    };

    Process::State processGetState(pid_type p) const
    {
        auto it = m_procs2.find(p);
        if (it == m_procs2.end())
            return Process::UNKNOWN;
        return it->second->state;
    }

protected:
    std::map<std::string, std::function<task_type(e_type)>> m_funcs;
    std::map<pid_type, std::shared_ptr<Process>> m_procs2;

    std::unordered_map<user_id_type, user_t> m_users = {
        //
        {
            0u, {.name = "root"} //
        }
        //
    };

    using awaiter_queue_type
        = moodycamel::ConcurrentQueue<std::pair<Awaiter *, std::shared_ptr<Process>>>;

    template<typename T>
    struct AwaiterQueue_T
    {
        using value_type = T;
        using queue_type = moodycamel::ConcurrentQueue<value_type>;

        queue_type &get() { return m_swap ? m_Q2 : m_Q1; }
        queue_type &get2() { return !m_swap ? m_Q2 : m_Q1; }

        void swap()
        {
            ++m_queueIteration;
            m_swap = !m_swap;
        }

        inline bool enqueue(value_type const &item) { return get().enqueue(item); }
        inline bool enqueue(value_type &&item) { return get().enqueue(std::move(item)); }
        inline bool try_dequeue(value_type &item) { return get().try_dequeue(item); }
        uint64_t const &queueRunIteration() const
        {
            //
            return m_queueIteration;
        }

        uint64_t m_queueIteration = 0;
        bool m_swap = false;
        queue_type m_Q1;
        queue_type m_Q2;
    };

    std::map<std::string, AwaiterQueue_T<std::pair<Awaiter *, std::shared_ptr<Process>>>> m_awaiters;

    pid_type _pid_count = 1;

public:
    static bool _has_flag(std::vector<std::string> &_args, std::string flag)
    {
        auto no_profile = std::find(_args.begin(), _args.end(), flag);
        if (no_profile == _args.end())
        {
            return false;
        }
        _args.erase(no_profile, no_profile + 1);
        return true;
    }

    static bool _has_arg(std::vector<std::string> &_args, std::string flag, std::string &argVal)
    {
        auto arg_it = std::find(_args.begin(), _args.end(), flag);
        if (arg_it == _args.end())
        {
            return false;
        }
        auto val_it = std::next(arg_it);
        if (val_it == _args.end())
            return false;

        argVal = *val_it;
        _args.erase(arg_it, val_it + 1);
        return true;
    }

protected:
    void setDefaultFunctions()
    {
#define PN_HANDLE_AWAIT_INT_TERM(returned_signal, CTRL)\
        switch(returned_signal)\
            {\
                case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT: { co_return static_cast<int>(PseudoNix::exit_interrupt);}\
                case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}\
                    default: break;\
            }

#define PN_HANDLE_AWAIT_BREAK_ON_SIGNAL(returned_signal, CTRL)\
            {auto returned_signal_value = returned_signal; \
            if(returned_signal_value == PseudoNix::AwaiterResult::SIGNAL_INTERRUPT) { break;}\
            if(returned_signal_value == PseudoNix::AwaiterResult::SIGNAL_TERMINATE) { break;}}

#define PN_HANDLE_AWAIT_TERM(returned_signal, CTRL)\
        switch(returned_signal)\
            {\
                case PseudoNix::AwaiterResult::SIGNAL_INTERRUPT:   CTRL->system->clearSignal(CTRL->get_pid()); break; \
                case PseudoNix::AwaiterResult::SIGNAL_TERMINATE: { co_return static_cast<int>(PseudoNix::exit_terminated);}\
                    default: break;\
            }

        // clang-format off
#define PN_PROC_START(control) \
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
            PseudoNix::FileSystem & FS = SYSTEM; (void)FS;\
            auto & pn_ctrl = control; (void)pn_ctrl;\
            auto SHELL_PROC = PARENT_SHELL_PID != PseudoNix::invalid_pid ? SYSTEM.getProcessControl(PARENT_SHELL_PID) : nullptr; (void)SHELL_PROC;\
            auto const U_ID = SYSTEM.getProcessUser(PID); (void)U_ID;
        // clang-format on

#define PN_SWITCH_QUEUE(...) \
    PN_HANDLE_AWAIT_INT_TERM(co_await pn_ctrl->await_yield(__VA_ARGS__), pn_ctrl)

// Wait for PIDs to finish
#define PN_WAIT(...) PN_HANDLE_AWAIT_TERM(co_await pn_ctrl->await_finished(__VA_ARGS__), pn_ctrl)

#define PN_PRINT(...) COUT << std::format(__VA_ARGS__)

#define PN_PRINTLN(...) \
    COUT << std::format(__VA_ARGS__); \
    COUT << '\n'

#define PN_YIELD_IF(DUR) \
    if (std::chrono::system_clock::now() - pn_ctrl->last_resume_time > DUR) \
    { \
        PN_HANDLE_AWAIT_INT_TERM(co_await pn_ctrl->await_yield(), pn_ctrl); \
    }

#define PN_YIELD() PN_YIELD_IF(SYSTEM.defeaultProcessTime())

// Used for doing quick checks.
// Check the condition, if its true, exits the coroutine
#define PN_PROC_CHECK(condition, ...)\
        if(condition)\
            {\
                    COUT << std::format("ERROR: {}: ", ARGS[0]);\
                    COUT << std::format(__VA_ARGS__);\
                    COUT << '\n';\
                    co_return 1;\
            }


#define PN_HANDLE_PATH(CWD, path)\
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
            PN_PROC_START(ctrl);

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
                PN_PRINT("{:15}: {:15}\n", f.first, (*funcDescs)[f.first]);
            }
            co_return 0;
        };
        DEF_FUNC_HELP("env", "Prints out all environment variables, or sets environment variables for other processes")
        {
            PN_PROC_START(ctrl);

            for(auto & [var, val] : ENV)
            {
                PN_PRINT("{}={}\n", var, val);
            }
            co_return 0;
        };
        DEF_FUNC_HELP("echo", "Prints arguments to standard output")
        {
            PN_PROC_START(ctrl);

            bool newline=true;
            // Handle -n option
            int start=1;
            if (ARGS.size() > 1 && std::string(ARGS[1]) == "-n") {
                newline = false;
                start=2;
            }

            PN_PRINT("{}", join(std::span(ARGS.begin() + start, ARGS.end()), " "));
            if(newline)
                COUT.put('\n');

            co_return 0;
        };
        DEF_FUNC_HELP("yes", "Keeps printing y to stdout until interrupted")
        {
            // A very basic example of a forever running
            // process
            PN_PROC_START(ctrl);

            while(true)
            {
                PN_PRINT("Y\n");
                //PN_PRINT("Y {}\n", SYSTEM.PROC_AT(PID)->should_pause);
                //if(SYSTEM.PROC_AT(PID)->should_pause)
                //{
                //    COUT << "Pausing\n";
                //    ctrl->system->PROC_AT(PID)->state = Process::SUSPENDED;
                //    co_await std::suspend_always{};
                //}

                PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
            }

            co_return 0;
        };
        DEF_FUNC_HELP("sleep", "Pauses for NUMBER seconds")
        {
            PN_PROC_START(ctrl);

            std::string output;
            if(ARGS.size() < 2)
                co_return 1;
            float t = 0.0f;
            to_number(ARGS[1], t);
            t = std::max(0.0f, t);
            // NOTE: do not acutally use this_thread::sleep
            // this is a coroutine, so you should suspend
            // the routine
            PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(std::chrono::milliseconds( static_cast<uint64_t>(t*1000))), ctrl);

            co_return 0;
        };

        (*funcDescs)["uptime"] = "Number of milliseconds since started";
        m_funcs["uptime"] = [T0=std::chrono::system_clock::now()](e_type ctrl) -> task_type
        {
            PN_PROC_START(ctrl);
            PN_PRINT("{}\n",
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now() - T0)
                         .count());
            co_return 0;
        };

        DEF_FUNC_HELP("rev", "Reverses the input")
        {
            PN_PROC_START(ctrl);

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
            PN_PROC_START(ctrl);

            uint32_t i=0;

            bool quit = false;
            while(!quit)
            {
                char c;
                PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_has_data(ctrl->in), ctrl);

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
            PN_PROC_START(ctrl);

            uint32_t i=0;
            for(auto & arg : ARGS)
            {
                PN_PRINT("[{:2}] {}\n", i, arg);
                ++i;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("ps", "Shows the current process list")
        {
            PN_PROC_START(ctrl);

            auto count_digits = [](auto n) {
                if (n == 0) return 1u;
                return static_cast<uint32_t>(std::log10(std::abs(static_cast<double>(n)))) + 1u;
            };

            uint32_t pid_width = 1;
            uint32_t queue_width = 5;
            uint32_t time_width = 4;
            for(auto & [pid, P] : SYSTEM.m_procs2)
            {
                auto ms_count = std::chrono::duration_cast<std::chrono::milliseconds>(P->process_time).count();
                time_width = std::max(time_width, count_digits(ms_count));
                pid_width = std::max(time_width, count_digits(pid));
                queue_width = std::max(queue_width, static_cast<uint32_t>(P->control->queue_name.size()));
            }

            PN_PRINT("{:>7} {:>{}} {:>1} {:<{}} {:>{}} {}\n",
                     "USER",
                     "PID",
                     pid_width,
                     "S",
                     "QUEUE",
                     queue_width,
                     "TIME",
                     time_width,
                     "CMD");

            std::string user_name;
            for(auto & [pid, P] : SYSTEM.m_procs2)
            {
                auto ms_count = std::chrono::duration_cast<std::chrono::milliseconds>(P->process_time).count();

                auto u_id = SYSTEM.getProcessUser(pid);
                if (SYSTEM.m_users.count(u_id))
                {
                    user_name = SYSTEM.m_users.at(u_id).name;
                }
                else
                {
                    user_name = std::to_string(u_id);
                }

                PN_PRINT("{:>7} {:>{}} {:>1} {:<{}} {:>{}} {}\n",
                         user_name,
                         pid,
                         pid_width,
                         P->state == Process::SUSPENDED ? "S" : "R",
                         P->control->queue_name,
                         queue_width,
                         ms_count,
                         time_width,
                         join(P->control->args));
            }

            co_return 0;
        };

        DEF_FUNC_HELP("kill", "Send signals to processes")
        {
            PN_PROC_START(ctrl);

            std::string sig_str = "2";

            std::vector<pid_type> pids;
            for(size_t i=1;i<ARGS.size(); i++)
            {
                auto & a = ARGS[i];
                if(a == "-h" || a == "--help")
                {
                    PN_PRINT("Usage:\n\n");
                    PN_PRINT("{} -<sig> pid1 pid2....:\n", ARGS[0]);
                    PN_PRINT("Usage:\n\n");
                    PN_PRINT("-<sig> can be one of:  \n");
                    PN_PRINT("   -{} or -{}\n", static_cast<int>(eSignal::INTERRUPT), "SIGINT");
                    PN_PRINT("   -{} or -{}\n", static_cast<int>(eSignal::KILL), "SIGKILL");
                    PN_PRINT("   -{} or -{}\n", static_cast<int>(eSignal::TERMINATE), "SIGTERM");
                    PN_PRINT("   -{} or -{}\n", static_cast<int>(eSignal::CONTINUE), "SIGCONT");
                    PN_PRINT("   -{} or -{}\n", static_cast<int>(eSignal::STOP), "SIGSTOP");
                    co_return 0;
                }
                else if(a.front() == '-')
                {
                    sig_str = std::string(a.begin()+1, a.end());
                }
                else
                {
                    pid_type pid;
                    PN_PROC_CHECK(!to_number(a, pid), "{} is not an Process ID\n", a);
                    pids.push_back(pid);
                }
            }


            eSignal ss = eSignal::NONE;
            if(sig_str == "2" || sig_str == "SIGINT")
                ss = eSignal::INTERRUPT;
            if(sig_str == "9" || sig_str == "SIGKILL")
                ss = eSignal::KILL;
            if(sig_str == "15" || sig_str == "SIGTERM")
                ss = eSignal::TERMINATE;
            if(sig_str == "18" || sig_str == "SIGCONT")
                ss = eSignal::CONTINUE;
            if(sig_str == "19" || sig_str == "SIGSTOP")
                ss = eSignal::STOP;

            PN_PROC_CHECK(ss==eSignal::NONE, "Invalid Signal");


            for(auto p : pids)
            {
                SYSTEM.signal(p, ss);
            }

            co_return 1;
        };

        DEF_FUNC_HELP("io_info", "Shows IO pointers")
        {
            PN_PROC_START(ctrl);

            for(auto & [pid, proc] : SYSTEM.m_procs2)
            {
                PN_PRINT("{}[{}]->{}->{}[{}]\n",
                         static_cast<void *>(proc->control->in.get()),
                         proc->control->in.use_count(),
                         proc->control->args[0],
                         static_cast<void *>(proc->control->out.get()),
                         proc->control->out.use_count());
            }
            co_return 0;
        };

        DEF_FUNC_HELP("to_std_cout", "Pipes process output to standard output")
        {
            PN_PROC_START(ctrl);
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
            PN_PROC_START(ctrl);

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
            PN_PROC_START(ctrl);
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
           PN_PROC_START(ctrl);

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
           PN_PROC_START(ctrl);
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
           PN_PROC_START(ctrl);

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

           PN_PROC_CHECK(!FS.exists(p), "cd: {}: No such file or directory\n", ARGS[1]);

           if(SHELL_PROC->chdir(p))
               co_return 0;

           PN_PRINT("Unknown error\n");
           co_return 1;

        };

        DEF_FUNC_HELP("sudo", "Runs a command a another user or as the root user")
        {
            // Executes a command N times.
            //
            //  Usage: sudo 0 echo hello world
            //
            PN_PROC_START(ctrl);

            uint32_t user_id = 0;

            std::vector<std::string> args;
            if (!to_number(ARGS[1], user_id))
            {
                args = std::vector(ARGS.begin() + 1, ARGS.end());
            }
            else
            {
                args = std::vector(ARGS.begin() + 2, ARGS.end());
            }

            auto E = System::parseArguments(args);

            E.in = ctrl->in;
            E.out = ctrl->out;
            auto pid = SYSTEM.runRawCommand(E, invalid_pid, true);

            PN_PROC_CHECK(pid == invalid_pid, "Error running command: {}", join(args, ","));

            SYSTEM.PROC_AT(pid)->user_id = user_id;

            SYSTEM.resume(pid);

            PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_finished(pid), ctrl);

            co_return 0;
        };

        DEF_FUNC_HELP("spawn", "Spawns N instances of the same process")
        {
            // Executes a command N times.
            //
            //  Usage: spawn 10 echo hello world
            //
            PN_PROC_START(ctrl);

            PN_PROC_CHECK(ARGS.size() < 2, "Error: \n\n  spawn [--count N] cmd <args...>\n");

            auto &args = ctrl->args;
            std::string count_str;
            size_t count = 1;
            if (_has_arg(args, "--count", count_str))
            {
                if (!count_str.empty())
                    PN_PROC_CHECK(to_number(count_str, count) == false,
                                  "Error: --count <ARG> must be a number");
            }

            count = std::clamp<size_t>(count, 0u, 1000u);

            while(count--)
            {
                auto E = System::parseArguments(std::vector(ARGS.begin() + 1, ARGS.end()));
                E.out = ctrl->out;
                ctrl->executeSubProcess(E);
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
            PN_PROC_START(ctrl);
            #if defined __EMSCRIPTEN__
            COUT << "This command does not work on Emscripten at the moment.\n";
            co_return 1;
            #endif
            std::string s;

            std::string TASK_QUEUE = ARGS.size() < 2 ? std::string("THREADPOOL") : ARGS[1];
            std::atomic<bool> stop_token = false;

            std::binary_semaphore _semaphore(0);

            PN_PROC_CHECK(!SYSTEM.taskQueueExists(TASK_QUEUE), "{}: Task queue, {}, does not exist", ARGS[0], TASK_QUEUE);

            PN_PROC_CHECK(TASK_QUEUE == SYSTEM.DEFAULT_QUEUE,
                          "{}: Cannot run background thread on {} queue.\n",
                          ARGS[0],
                          TASK_QUEUE);

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
                PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
            }

            co_return 0;
        };

        DEF_FUNC_HELP("queue", "Create/List/Destroy task queues")
        {
            // Lists all the queues and the number of tasks
            // currently in the queue
            PN_PROC_START(ctrl);
            if(ARGS.size() == 1)
            {
                PN_PRINT("Error in arguments:\n\n");
                PN_PRINT("Usage: {} [list|create|destroy] <queue name>\n", ARGS[0]);
                co_return 1;
            }
            if( ARGS[1] == "list")
            {
                for(auto & a : SYSTEM.m_awaiters)
                {
                    PN_PRINT("{} {}\n", a.first, a.second.get().size_approx());
                }
                co_return 0;
            }
            if( ARGS[1] == "create" )
            {
                if(ARGS.size() != 3)
                {
                    PN_PRINT("Requires a name for the queue\n");
                    co_return 1;
                }
                SYSTEM.m_awaiters[ARGS[2]];
                co_return 0;
            }
            if( ARGS[1] == "destroy" )
            {
                if(ARGS.size() != 3)
                {
                    PN_PRINT("Requires a name for the queue\n");
                    co_return 1;
                }
                if (ARGS[2] == "MAIN")
                {
                    PN_PRINT("Error: Cannot destroy the HOME queue\n");
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
            PN_PROC_START(ctrl);
            using namespace std::chrono_literals;
            if( ARGS.size() < 2)
            {
                PN_PRINT("Requires a Task Queue name\n\n   queueHopper <queue name>");
                co_return 1;
            }
            std::string TASK_QUEUE = ARGS[1];

            if(!SYSTEM.taskQueueExists(TASK_QUEUE))
            {
                PN_PRINT("Task queue, {}, does not exist. The Task Queue needs to be created using "
                         "'queue create <name>' ",
                         TASK_QUEUE);
                co_return 1;
            }

            PSEUDONIX_TRAP {
                PN_PRINT("Trap on {} queue\n", QUEUE);
            };

            {
                auto _lock = COUT.lock();
                PN_PRINT("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
            }

            // the QUEUE variable defined by PN_PROC_START(ctrl)
            // tells you what queue this process is being executed on
            PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(250ms, SYSTEM.defaultQueue()),
                                     ctrl);

            {
                auto _lock = COUT.lock();
                PN_PRINT("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
            }

            for(int i=0;i<10;i++)
            {
                // wait for 1 second and then resume on a different Task Queue
                // Specific task queues are executed at a specific time
                PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(250ms, TASK_QUEUE), ctrl);

                {
                    // Only one thread can read/write to the pipes at a time
                    // it is quite likely that COUT is being shared by multiple
                    // processes. So we ensure that we lock access
                    // to it so that it doesn't cause any race conditions
                    auto _lock = COUT.lock();
                    PN_PRINT("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
                }

                PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(500ms,
                                                                        SYSTEM.defaultQueue()),
                                         ctrl);

                {
                    auto _lock = COUT.lock();
                    PN_PRINT("On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
                }
            }

            // finally make sure we are on the main queue
            // when we exit so that the TRAP function will be executed
            // on that
            PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(500ms, SYSTEM.defaultQueue()),
                                     ctrl);
            {
                auto _lock = COUT.lock();
                PN_PRINT("Last On {} queue. Thread ID: {}\n", QUEUE, ENV["THREAD_ID"]);
            }

            co_return 0;
        };


        DEF_FUNC_HELP("pwd", "Prints the current working directory")
        {
            PN_PROC_START(ctrl);
            PN_PRINT("{}\n", ctrl->cwd.generic_string());
            co_return 0;
        };

#define FS_PRINT_ERROR(_error) \
    switch (_error) { \
    case FSResult::False: \
    case FSResult::True: \
        break; \
    case FSResult::ErrorNotEmpty: \
        PN_PRINT("Location is not empty"); \
        break; \
    case FSResult::ErrorReadOnly: \
        PN_PRINT("Location is read-only"); \
        break; \
    case FSResult::ErrorNotFile: \
        PN_PRINT("Location is not a file"); \
        break; \
    case FSResult::ErrorNotDirectory: \
        PN_PRINT("Location is not a directory"); \
        break; \
    case FSResult::ErrorDoesNotExist: \
        PN_PRINT("File or folder does not exists"); \
        break; \
    case FSResult::ErrorExists: \
        PN_PRINT("File or folder already exists"); \
        break; \
    case FSResult::ErrorParentDoesNotExist: \
        PN_PRINT("Unknown Error"); \
        break; \
    case FSResult::ErrorIsMounted: \
        PN_PRINT("Location is a mounted file/directory"); \
        break; \
    case FSResult::UnknownError: \
        PN_PRINT("Unknown Error"); \
        break; \
    }

        DEF_FUNC_HELP("ls", "Lists files and directories")
        {
            PN_PROC_START(ctrl);
            path_type path = CWD;

            if(ARGS.size() >= 2)
            {
                path_type p = ARGS[1];
                PN_HANDLE_PATH(CWD, p)
                path = p;
            }

            assert(path.has_root_directory());

            for(auto u : SYSTEM.list_dir(path))
            {
                auto ref = FS.fs(path/u);
                auto typ = ref.get_type();
                if(typ == NodeType::Custom)
                {
                    auto n = ref.file_node();
                    if(auto ff = std::any_cast<float>(&n->custom))
                    {
                        PN_PRINT("{}: {}\n", u.generic_string(), *ff);
                    }
                    else if(auto fu = std::any_cast<uint32_t>(&n->custom))
                    {
                        PN_PRINT("{}: {}\n", u.generic_string(), *fu);
                    }
                    else if(auto fi = std::any_cast<int32_t>(&n->custom))
                    {
                        PN_PRINT("{}: {}\n", u.generic_string(), *fi);
                    }
                    else
                    {
                        PN_PRINT("[c] {}\n", u.generic_string());
                    }
                }
                else if (typ == NodeType::MountDir || typ == NodeType::MemDir)
                {
                    PN_PRINT("[d] {}/\n", u.generic_string());
                }
                else if (typ == NodeType::MountFile || typ == NodeType::MemFile )
                {
                    PN_PRINT("[f] {}\n", u.generic_string());
                }
            }

            co_return 0;
        };

        DEF_FUNC_HELP("mkdir", "Create directories")
        {
            PN_PROC_START(ctrl);
            path_type path = "/";

            bool parents = _has_flag(ctrl->args, "-p");

            if (ARGS.size() >= 2)
            {
                path = ARGS[1];
                PN_HANDLE_PATH(CWD, path);

                auto res = parents ? SYSTEM.mkdirs(path) : SYSTEM.mkdir(path);

                FS_PRINT_ERROR(res);
            }
            else
            {
                PN_PRINT("mkdir: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };


        DEF_FUNC_HELP("rm", "Removes files and directories")
        {
            PN_PROC_START(ctrl);
            path_type path = "/";
            if(ARGS.size() >= 2)
            {
                for(size_t i=1; i < ARGS.size();i++)
                {
                    path = ARGS[i];
                    PN_HANDLE_PATH(CWD, path);
                    if(!SYSTEM.remove(path))
                    {
                        PN_PRINT("Error deleting file: {}", path.generic_string());
                        co_return 1;
                    }
                }
            }
            else
            {
                PN_PRINT("touch: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("touch", "Create files")
        {
            PN_PROC_START(ctrl);
            path_type path = "/";
            if(ARGS.size() >= 2)
            {
                for(size_t i=1; i < ARGS.size();i++)
                {
                    path = ARGS[i];
                    PN_HANDLE_PATH(CWD, path);
                    auto res = SYSTEM.mkfile(path);
                    FS_PRINT_ERROR(res);
                }
            }
            else
            {
                PN_PRINT("touch: missing operand\n");
                co_return 1;
            }

            co_return 0;
        };

        DEF_FUNC_HELP("cp", "Copies files and directories")
        {
            PN_PROC_START(ctrl);
            if(ARGS.size() >= 3)
            {
                path_type cpy_to = ARGS.back();
                PN_HANDLE_PATH(CWD, cpy_to);

                for(size_t i=1; i<ARGS.size()-1;i++)
                {
                    path_type path = ARGS[i];
                    _clean(path);
                    PN_HANDLE_PATH(CWD, path);
                    SYSTEM.copy(path, cpy_to);
                }
            }
            else
            {
                PN_PRINT("touch: missing operand\n");
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

            PN_PROC_START(ctrl);

            int ret_value = 1;

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
                            PN_PRINT("{} on {}\n", d->mount->get_info(), c.generic_string());
                        }
                    }
                }
            }
            else if(ARGS.size() == 4)
            {
                auto TYPE = ARGS[1];
                System::path_type SRC  = ARGS[2];
                System::path_type DST  = ARGS[3];
                PN_HANDLE_PATH(CWD, DST);
                PN_HANDLE_PATH(CWD, SRC);

                std::vector<std::string> mount_args = { TYPE, "mount"};
                for(size_t i=2;i<ARGS.size();i++)
                {
                    mount_args.push_back(ARGS[i]);
                }
                auto E = System::parseArguments(mount_args);
                E.in = ctrl->in;
                E.out = ctrl->out;

                auto s_pid = ctrl->executeSubProcess(E);
                PN_HANDLE_AWAIT_TERM( co_await ctrl->await_finished(s_pid), ctrl);

                if(!to_number(ENV["?"], ret_value))
                {
                    co_return 0;
                }
            }

            co_return std::move(ret_value);
        };

        DEF_FUNC_HELP("umount", "Unmounts a host filesystem")
        {
            PN_PROC_START(ctrl);

            PN_PROC_CHECK(SYSTEM.getProcessUser(PID) != 0, "Must be run as user 0");
            //PN_PROC_CHECK()
            if (ARGS.size() == 2)
            {
                path_type p = ARGS[1];
                if(!p.has_root_directory())
                    p = CWD / p;
                auto res = SYSTEM.unmount(p);
                FS_PRINT_ERROR(res);
                co_return 0;
            }
            PN_PRINT("Unknown error\nUsage:\n umount <mount point>\n");

            co_return 1;
        };
#endif
        DEF_FUNC_HELP("test", "Test file types and compares values")
        {
            // very simple implemntation of the "test" function in linux
            // mostly used in if statements in shell scripts
            PN_PROC_START(ctrl);
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
                        PN_PRINT("test: {}: integer expression expected\n", left);
                        co_return 2;
                    }
                    if(!to_number(right, right_int))
                    {
                        PN_PRINT("test: {}: integer expression expected\n", right);
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
            PN_PROC_START(ctrl);

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
                                PN_PRINTLN("{}", line);
                            }
                            if(file.eof())
                                break;
                            PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
                            T0 = std::chrono::system_clock::now();
                        }
                        co_return 0;
                    }
                    default:
                    {
                        PN_PRINT("cat: {}: Not a regular file", ARGS[1]);
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
            PN_PROC_START(ctrl);

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
        auto & proc = PROC_AT(pid);

        proc->state = Process::AWAITING;
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

        // set the state so that
        // the process will be removed
        coro.state = Process::FINALIZED;
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
            if (a.second->force_terminate || a.second->is_complete || a.second->state == Process::FINALIZED)
                return found;

            if (a.second->state == Process::SUSPENDING)
            {
                a.second->state = Process::SUSPENDED;
            }
            else if (a.second->state == Process::RESUMING)
            {
                a.second->state = Process::AWAITING;
            }

            if (a.second->state == Process::AWAITING && a.first->await_ready())
            {
                a.second->control->env["QUEUE"] = queue_name;
                _resume_task_now(a.second);
            }
            else
            {
                PUSH_Q.enqueue(std::move(a));
            }
        }
        return found;
    }

    /**
     * @brief _resume_task
     * @param P
     * 
     * Anytime there is a resuming of task, it must go through this
     * function, any calls to task.resume() or awaiter.resume() is
     * incorrect.
     */
    void _resume_task_now(std::shared_ptr<Process> &P)
    {
        P->control->env["THREAD_ID"] = std::format("{}", std::this_thread::get_id());
        DEBUG_SYSTEM("  Resuming on QUEUE: {} PID: {} : {}",
                     queue_name,
                     a.second->control->pid,
                     join(a.second->control->args));

        P->state = Process::RUNNING;

        auto &last_resume_time = P->control->last_resume_time;
        last_resume_time       = std::chrono::system_clock::now();
        P->task.resume();
        P->process_time += std::chrono::system_clock::now() - last_resume_time;

        if (P->task.done())
        {
            auto exit_code     = P->task();
            P->is_complete     = true;
            *P->exit_code      = !P->force_terminate ? exit_code : -1;
            //P->should_remove   = true;
            P->force_terminate = true;
            P->state           = Process::EXITED;
        }
    }
};
}


#endif

