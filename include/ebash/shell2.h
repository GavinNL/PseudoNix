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
                if(c == '(')
                {
                    pos+=1;
                    return std::string(1,c);
                }
                else if(c==')')
                {
                    if(!current.empty())
                        return current;
                    pos+=1;
                    return std::string(1,c);
                }
                if(sub == "&&" || sub == "||" || sub == "$(")
                {
                    pos+=2;
                    return std::string(sub);
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


//MiniLinux::task_type  execute_no_brackets(std::vector<std::string> tokens, std::string &std_out)
MiniLinux::task_type execute(std::vector<std::string> tokens,
                               MiniLinux* mini,
                               std::shared_ptr<MiniLinux::stream_type> in={},
                               std::shared_ptr<MiniLinux::stream_type> out={})
{
    MiniLinux::Exec E;
    E.args = tokens;
    E.in   = in;
    E.out  = out;

    auto pid = mini->runRawCommand2(E);
    if(pid != 0xFFFFFFFF)
    {
        auto f = mini->getProcessFuture(pid);
        while( f.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            co_await std::suspend_always{};
        }
        co_return f.get();
    }
    co_return 127;
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
