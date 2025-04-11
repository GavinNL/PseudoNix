#ifndef EBASH_SHELL2_H
#define EBASH_SHELL2_H

#include <map>
#include <future>
#include "MiniLinux.h"


namespace bl
{

struct Tokenizer2
{
    std::string_view input;
    std::string _next;
    size_t pos = 0;

    Tokenizer2(std::string_view in) : input(in)
    {
    }

    static std::vector<std::string> to_vector(std::string_view _out)
    {
        std::vector<std::string> tokens;
        Tokenizer2 T2(_out);
        auto t2=T2.next();
        while(!t2.empty())
        {
            tokens.push_back(t2);
            t2 = T2.next();
        }
        return tokens;
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

    std::string next()
    {
        std::string current;
        bool quoted = false;

        while(pos < input.size())
        {
            char c = input[pos];
            auto sub = input.substr(pos, 2);

            if(!quoted)
            {
                if(sub == "&&" || sub == "||" || sub == "$(")
                {
                    if(!current.empty())
                        return current;
                    pos+=2;
                    return std::string(sub);
                }
                else if(c==')' || c == '|' || c=='(')
                {
                    if(!current.empty())
                        return current;
                    pos+=1;
                    return std::string(1,c);
                }
                else if( c == '"')
                {
                    quoted = !quoted;
                    pos += 1;
                }
                else
                {
                    if( !std::isspace(c) )
                    {
                        current += c;
                        pos += 1;
                    }
                    else
                    {
                        if(!current.empty())
                            return current;
                        else
                            pos += 1;
                    }
                }
            }
            else
            {
                if( c == '"')
                    quoted = !quoted;
                else
                {
                    current += c;
                }
                pos += 1;
            }

        }
        if(!current.empty())
        {
            auto s = std::move(current);
            return s;
        }
        return {};
    }

};


struct ShellEnv
{
    std::string                        rc_text;
    std::map<std::string, bool>        exportedVar;
    std::map<std::string, std::string> env;
    bool                               exitShell = false;

    // Shell tasks are similar to the normal functions
    // but they are only executed inside the "sh" function
    //
    // They do not sure up under the process list
    std::map<std::string, std::function<MiniLinux::task_type(MiniLinux::e_type, ShellEnv*) >  > shellFuncs = {
        {
            "exit",
            [](MiniLinux::e_type ex, ShellEnv * shell) -> MiniLinux::task_type
            {
                (void)ex;
                shell->exitShell = true;
                co_return 0;
            }
        },
        {
            "", // empty function
            [](MiniLinux::e_type ex, ShellEnv * shell) -> MiniLinux::task_type
            {
                (void)shell;
                (void)ex;
                // This function will be called, if we set environment variables
                // but didn't call an actual function, eg:
                //     VAR=value VAR2=value2
                for(auto & [var,val] : ex->env)
                {
                    shell->env[var] = val;
                }
                co_return 0;
            }
        },
        {
            "export",
            [](MiniLinux::e_type ex, ShellEnv * shell) -> MiniLinux::task_type
            {
                // used to export variables
                for(size_t i=1;i<ex->args.size();i++)
                {
                    auto [var,val] = Tokenizer2::splitVar(ex->args[i]);
                    if(!var.empty() && !val.empty())
                    {
                        // if the arg looked like: VAR=VAL
                        // then set the variable as well as
                        // export it
                        shell->exportedVar[std::string(var)] = true;
                        shell->env[std::string(var)] = val;
                    }
                    else
                    {
                        // just export the variable
                        shell->exportedVar[std::string(ex->args[i])] = true;
                    }
                }
                co_return 0;
            }
        },
        {
            "exported",
            [](MiniLinux::e_type ex, ShellEnv * shell) -> MiniLinux::task_type
            {
                // used to export variables
                (void)ex;
                for(auto & x : shell->exportedVar)
                {
                    *ex->out << x.first << '\n';
                }
                co_return 0;
            }
        }
    };
};


MiniLinux::task_type execute_pipes(std::vector<std::string> tokens,
                             MiniLinux* mini,
                             ShellEnv * exported_environment,
                             std::shared_ptr<MiniLinux::stream_type> in={},
                             std::shared_ptr<MiniLinux::stream_type> out={})
{


    auto first = tokens.begin();
    auto last = std::find(first, tokens.end(), "|");

    std::vector<MiniLinux::Exec> E(1);

    while(last != tokens.end())
    {
        E.back().args = std::vector(first, last);
        E.back().in   = nullptr;
        E.back().out  = nullptr;

        first = last+1;
        last = std::find(first, tokens.end(), "|");
        E.push_back({});
    }
    E.back().args = std::vector(first, last);


    E.front().in = in;
    E.back().out = out;

    for(size_t i=1;i<E.size();i++)
    {
        E[i-1].out = MiniLinux::make_stream();
        E[i].in = E[i-1].out;
    }

    std::vector<std::future<int>> _futures;

    std::map<std::string, std::string> env;
    for(auto & [name, exp] : exported_environment->exportedVar)
    {
        if(exported_environment->env.count(name))
            env[name] = exported_environment->env[name];
    }

    std::vector<MiniLinux::task_type> _shellTasks;
    for(auto & e : E)
    {
        // copy the default exported variables
        e.env = env;


        //====================================================
        // Find all the environment variables
        // that are being passed directly into the
        // command: ex:  "CC=gcc make"
        //====================================================
        while(e.args.size())
        {
            auto [var,val] = Tokenizer2::splitVar(*e.args.begin());
            if(!var.empty())
            {
                e.env[std::string(var)] = val;
                e.args.erase(e.args.begin());
                continue;
            }
            break;
        }

        // we set variables, but didnt call a function, eg: CC=gcc CXX=g++
        if(e.args.empty())
        {
            e.args.push_back("");
        }
        //====================================================

        // Try to find the shell function first
        // to see if it exists
        auto it = exported_environment->shellFuncs.find(e.args[0]);
        if(it != exported_environment->shellFuncs.end())
        {
            auto control = std::make_shared<MiniLinux::ProcessControl>();
            control->mini = mini;
            control->in = e.in;
            control->out = e.out;
            control->env = e.env;
            control->args = e.args;

            _shellTasks.push_back(it->second(control, exported_environment));
        }
        else
        {
            auto pid = mini->runRawCommand(e);

            if(pid != 0xFFFFFFFF)
            {
                _futures.push_back(mini->getProcessFuture(pid));
            }
            else
            {
                *out << e.args[0] << ": command not found.\n";
                std::cout << e.args[0] << std::endl;
            }
        }
    }

    while(true)
    {
        size_t count=0;

        // wait for all shell tasks as well
        for(auto & _t : _shellTasks)
        {
            if(!_t.done())
            {
                _t.resume();
                continue;
            }
            else
            {
                ++count;
            }
        }
        for(auto & f : _futures)
        {
            count += f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        if(count != _futures.size() + _shellTasks.size())
        {
            co_await std::suspend_always{};
            continue;
        }
        break;
    }
    int ret_value = 0;
    for(auto & f : _futures)
        ret_value = f.get();
    co_return ret_value;
}

MiniLinux::task_type execute_no_brackets(std::vector<std::string> tokens,
                                         MiniLinux* mini,
                                         ShellEnv * exported_environment,
                                         std::shared_ptr<MiniLinux::stream_type> in={},
                                         std::shared_ptr<MiniLinux::stream_type> out={})
{
    std::vector<std::string> args;

    int ret_value = 0;
    for(size_t i=0; i<tokens.size(); i++)
    {
        auto & t = tokens[i];

        if(t == "&&")
        {
            auto _task = execute_pipes(args, mini, exported_environment, in, out);
            args.clear();

            while(!_task.done())
            {
                _task.resume();
                co_await std::suspend_always{};
            }
            ret_value = _task();

            if(ret_value != 0)
            {
                while(i < tokens.size() && tokens[i] != "||" )
                    i++;
            }
        }
        else if(t == "||")
        {
            auto _task = execute_pipes(args, mini, exported_environment, in, out);
            args.clear();
            while(!_task.done())
            {
                _task.resume();
                co_await std::suspend_always{};
            }
            ret_value = _task();

            if(ret_value == 0)
            {
                while(i < tokens.size() && tokens[i] != "&&" )
                    i++;
            }
        }
        else
        {
            args.push_back(t);
        }

    }
    if(args.size())
    {
        auto _task = execute_pipes(args, mini, exported_environment, in, out);
        args.clear();
        while(!_task.done())
        {
            _task.resume();
            co_await std::suspend_always{};
        }
        ret_value = _task();
    }

    co_return ret_value;
}

#if 1

MiniLinux::task_type execute_brackets(std::vector<std::string> tokens,
                                      MiniLinux* mini,
                                      ShellEnv * exported_environment,
                                      std::shared_ptr<MiniLinux::stream_type> in={},
                                      std::shared_ptr<MiniLinux::stream_type> out={})
{
    std::vector<std::string> args;

    int ret_value = 0;
    for(size_t i=0; i<tokens.size(); i++)
    {
        auto & t = tokens[i];

        if(t == ")")
        {
            auto rit = std::find_if(args.rbegin(), args.rend(), [](auto &&d)
                                    {
                                        return d=="$(" || d=="(";
                                    });
            if(rit != args.rend())
            {

                std::vector v(rit.base(), args.end());
                args.erase(rit.base(), args.end());
                if(args.back() == "$(")
                {
                    auto _in = MiniLinux::make_stream();
                    auto _out = MiniLinux::make_stream();
                    _in->close();

                    auto _task = execute_no_brackets(v, mini, exported_environment, _in, _out);
                    while(!_task.done())
                    {
                        _task.resume();
                        co_await std::suspend_always{};
                    }

                    std::string out_str;
                    *_out >> out_str;

                    auto vv = Tokenizer2::to_vector(out_str);
                    args.pop_back();
                    for(auto & _v : vv)
                        args.push_back(_v);
                }
            }
        }
        else
        {
            args.push_back(t);
        }
    }
    if(args.size())
    {
        auto _task = execute_no_brackets(args, mini, exported_environment, in, out);
        while(!_task.done())
        {
            _task.resume();
            co_await std::suspend_always{};
        }

        ret_value = _task();
    }

    co_return ret_value;
}

#endif


/**
 * @brief var_sub
 * @param str
 * @param env
 * @return
 *
 * Given a string that contains ${VARNAME} or $VARNAME, and the env map, substitue
 * the appropriate variables and return a new string.
 */
std::string var_sub1(std::string_view str, std::map<std::string,std::string> const & env)
{
    (void)env;
    std::string outstr;

    for(size_t i=0;i<str.size();i++)
    {
        auto sub = str.substr(i,2);
        if(sub == "${" || (sub[0]=='$' && std::isalpha(sub[1])) )
        {
            std::string var_name;
            for(i=i+1; i<str.size(); i++)
            {
                if(str[i] == '}' || std::isspace(str[i]))
                {
                    break;
                }
                var_name += str[i];
            }
            auto it = env.find((var_name.size() && var_name.front() == '{') ? var_name.substr(1) : var_name);
            if(it != env.end())
                outstr += it->second;
        }
        else
        {
            outstr += str[i];
        }
    }
    return outstr;
}


MiniLinux::task_type shell2(MiniLinux::e_type control, ShellEnv shellEnv = {})
{
    std::string _current;
    //static int count = 0;
    //count++;
    int ret_value = 0;

    auto & exev = *control;

    // Copy the rc_text into the
    // the input stream so that
    // it will be executed first
    *exev.in << shellEnv.rc_text;

    // Copy the additional environment variables
    // into our workinv variables
    for(auto & [var, val] : exev.env)
    {
        shellEnv.env[var] = val;
    }

    while(!shellEnv.exitShell && !exev.is_sigkill() && !exev.in->eof())
    {
        while(!exev.in->has_data() && !exev.is_sigkill())
        {
            co_await std::suspend_always{};
        }
        auto c = exev.in->get();
        _current += c;
        if(c != ';' && c != '\n')
        {
            continue;
        }
        _current.pop_back();

        if(_current.empty())
            continue;


        _current = var_sub1(_current, shellEnv.env);
        auto args = Tokenizer2::to_vector(_current);
        _current.clear();

        bool run_in_background = false;
        (void)run_in_background;

        if(args.back() == "&")
        {
            run_in_background = true;
            args.erase(args.end()-1);
        }
        else if(args.back().back() == '&')
        {
            args.back().pop_back();
            run_in_background = true;
        }

        auto _task = execute_brackets(args,
                                      exev.mini,
                                      &shellEnv,
                                      exev.in,
                                      exev.out);

        if(run_in_background)
        {
            exev.mini->registerProcess(std::move(_task), std::make_shared<MiniLinux::ProcessControl>());
        }
        else
        {
            while(!_task.done())
            {
                _task.resume();
                co_await std::suspend_always{};
            }
            ret_value = _task();
            shellEnv.env["?"] = std::to_string(ret_value);
        }
    }

    co_return 0;
}


}

#endif
