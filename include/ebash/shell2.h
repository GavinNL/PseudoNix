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

struct ShellEnv;
inline std::map<uint32_t, ShellEnv> _shells;

struct ShellEnv
{
    std::string                        rc_text;
    std::map<std::string, bool>        exportedVar;
    std::map<std::string, std::string> env;
    bool                               exitShell = false;
    MiniLinux::pid_type                shellPID = 0;
    bool                               isSigTerm = false;

    // child PIDs
    struct Internal_
    {
        std::vector<MiniLinux::pid_type>   pids;
        //std::vector<std::future<int>>   futures;
    };
    std::shared_ptr<Internal_> internal = std::make_shared<Internal_>();


    static void setFuncs(MiniLinux * L)
    {
        #define _GET_SHELL \
                MiniLinux::pid_type shell_pid = static_cast<MiniLinux::pid_type>(std::stoul(ex->env["SHELL_PID"]));\
                    auto *shell = &_shells[shell_pid];\
                    if(!shell)\
                    co_return 0;\

        auto & f = L->m_funcs;

        f["exit"] = [](MiniLinux::e_type ex) -> MiniLinux::task_type
            {
                _GET_SHELL

                shell->exitShell = true;
                co_return 0;
        };
        f[""] = [](MiniLinux::e_type ex) -> MiniLinux::task_type
        {
            _GET_SHELL
            (void)ex;
            // This function will be called, if we set environment variables
            // but didn't call an actual function, eg:
            //     VAR=value VAR2=value2
            for(auto & [var,val] : ex->env)
            {
                shell->env[var] = val;
            }
            co_return 0;
        };
        f["export"] = [](MiniLinux::e_type ex) -> MiniLinux::task_type
        {
            _GET_SHELL

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
        };

        f["exported"] = [](MiniLinux::e_type ex) -> MiniLinux::task_type
        {
            _GET_SHELL
            // used to export variables
            (void)ex;
            for(auto & x : shell->exportedVar)
            {
                *ex->out << x.first << '\n';
            }
            co_return 0;
        };
    }
};


std::vector<MiniLinux::pid_type> execute_pipes(std::vector<std::string> tokens,
                             MiniLinux* mini,
                             ShellEnv * exported_environment,
                             std::shared_ptr<MiniLinux::stream_type> in={},
                             std::shared_ptr<MiniLinux::stream_type> out={})
{
    auto first = tokens.begin();
    auto last = std::find(first, tokens.end(), "|");

    std::vector<std::vector<std::string>> list_of_args;

    while(last != tokens.end())
    {
        list_of_args.push_back(std::vector(first, last));
        first = last+1;
        last = std::find(first, tokens.end(), "|");
    }
    list_of_args.push_back(std::vector(first, last));


    auto E = MiniLinux::genPipeline(list_of_args);
    E.front().in = in;
    E.back().out = out;

    // Copy all the exported varaibles to
    // each of the processes we're going to be running
    for(auto & [name, exp] : exported_environment->exportedVar)
    {
        if(exported_environment->env.count(name))
        {
            for(auto & e : E)
                e.env[name] = exported_environment->env[name];
        }
    }

    for(auto & e : E)
    {
        // Copy the SHELL_PID value so that
        // calling functions can know which shell its running under
        e.env["SHELL_PID"] = std::to_string(exported_environment->shellPID);

        // we set variables, but didnt call a function, eg: CC=gcc CXX=g++
        //
        // We can use this to set environment variables for the shell
        //
        if(e.args.empty())
        {
            e.args.push_back("");
        }
    }

    auto pids = mini->runPipeline(E);

    return pids;
}


std::vector< std::vector<std::string> > parse_operands(std::vector<std::string> tokens)
{
    std::vector< std::vector<std::string> > args(1);
    args.back().push_back(")(");
    for(auto & a : tokens)
    {
        if( a == "&&" || a == "||" )
        {
            args.push_back({});
        }
        args.back().push_back(a);
    }
    return args;
}

#if 0

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


MiniLinux::task_type shell2(MiniLinux::e_type control, ShellEnv shellEnv1 = {})
{
    std::string _current;
    //static int count = 0;
    //count++;
    int ret_value = 0;

    auto & exev = *control;

    auto shellPID = control->get_pid();
    auto & shellEnv = _shells[shellPID];

    shellEnv = shellEnv1;
    ShellEnv::setFuncs(control->mini);
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
    shellEnv.shellPID = control->get_pid();
    assert(shellEnv.shellPID != 0xFFFFFFFF);



    while(true)
    {
        if(exev.is_sigkill()) break;
        if(exev.in->eof()) break;
        if(shellEnv.exitShell) break;

        while(!exev.has_data())
        {
            co_await std::suspend_always{};
            if(exev.is_sigkill()) co_return 0;
        }

        auto c = exev.get();
        _current += c;
        if(c != ';' && c != '\n')
        {
            continue;
        }
        _current.pop_back();

        if(_current.empty())
            continue;

        auto args = Tokenizer2::to_vector(var_sub1(_current, shellEnv.env));
        _current.clear();

        bool run_in_background = false;

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


#if 0
        auto _task = execute_brackets(args,
                                      exev.mini,
                                      &shellEnv,
                                      exev.in,
                                      stdout);
#endif
        auto op_args = parse_operands(args);

        std::reverse(op_args.begin(), op_args.end());

        int ret_value = 0;
        while(op_args.size())
        {
            auto & _a = op_args.back();
            auto op = _a.front();

            if(op == "&&" && ret_value != 0)
            {
                op_args.pop_back();
                continue;
            }
            if(op == "||" && ret_value == 0)
            {
                op_args.pop_back();
                continue;
            }

            auto stdout = MiniLinux::make_stream();
            auto pids = execute_pipes( std::vector(_a.begin()+1, _a.end()), exev.mini, &shellEnv, exev.in, stdout);
            auto f = exev.mini->getProcessFuture(pids.back());

            // pids are running in the foreground:
            while(!exev.mini->isAllComplete(pids))
            {
                co_await std::suspend_always{};
                *exev.out << *stdout;
            }

            std::cerr << std::endl;
            ret_value = f.get();

            op_args.pop_back();
        }
    }

    co_return 0;
}


}

#endif
