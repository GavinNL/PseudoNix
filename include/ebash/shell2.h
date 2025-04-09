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


MiniLinux::task_type execute(std::vector<std::string> tokens,
                               MiniLinux* mini,
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
    for(auto & e : E)
    {
        auto pid = mini->runRawCommand2(e);
        if(pid != 0xFFFFFFFF)
        {
            _futures.push_back(mini->getProcessFuture(pid));
        }
    }

    while(true)
    {
        size_t count=0;
        for(auto & f : _futures)
        {
            count += f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
        if(count != _futures.size())
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
            auto _task = execute(args, mini, in, out);
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
            auto _task = execute(args, mini, in, out);
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
        auto _task = execute(args, mini, in, out);
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

                    auto _task = execute_no_brackets(v, mini, _in, _out);
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
        auto _task = execute_no_brackets(args, mini, in, out);
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

MiniLinux::task_type shell2(MiniLinux::Exec exev)
{
    std::string _current;
    static int count = 0;
    count++;

    int shell_number = count;


    exev << "-------------------------\n";
    exev << std::format("Welcome to shell: {}\n",shell_number);
    exev << "-------------------------\n";
    exev << std::format("{}", exev.env["PROMPT"]);

    while(!exev.is_sigkill() && !exev.in->eof())
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


        _current = var_sub1(_current, exev.env);
        auto args = Tokenizer2::to_vector(_current);

        _current.clear();
        auto _task = execute_brackets(args, exev.control->mini, exev.in, exev.out);
        while(!_task.done())
        {
            _task.resume();
            co_await std::suspend_always{};
        }
    }
    co_return 0;

}

}

#endif
