#ifndef PSEUDONIX_SHELL2_H
#define PSEUDONIX_SHELL2_H

#include "System.h"
#include "generator.h"
#include <string>
#include <memory>

namespace PseudoNix
{

Generator< std::string > BashTokenizerGen2(std::shared_ptr<System::stream_type> in)
{
    std::string _token;
    if(!in->has_data())
        co_yield std::string();

    char c = ' ';

    while(true)
    {
        auto res = in->get(&c);

        if(res == System::stream_type::Result::END_OF_STREAM)
        {
            if(!_token.empty())
                co_yield _token;
            co_yield ";";
            co_yield "done";
            co_return;
        }
        if(res == System::stream_type::Result::EMPTY)
            co_yield std::string();


        if(c == ';' || c == '\n')
        {
            if(!_token.empty())
                co_yield _token;
            co_yield std::string(";");
            _token.clear();
        }
        else if( std::isspace(c) )
        {
            if(!_token.empty())
            {
                co_yield _token;
                _token.clear();
            }
        }
        else
        {
            _token.push_back(c);
        }
    }

    co_return;
}


using WhatToDo = std::variant< std::vector<System::pid_type>, int>;

Generator<WhatToDo> parse_if(Generator<std::string> & gen, Generator<std::string>::iterator & a);
Generator<WhatToDo> parse_args(std::string cmd, Generator<std::string> & gen, Generator<std::string>::iterator & a);

Generator<WhatToDo> parse_block(Generator<std::string> & gen, Generator<std::string>::iterator & a)
{
    //auto gen = BashTokenizerGen2(in);
    //auto a = gen.begin();

    std::vector<std::string> condition;
    while(a != gen.end())
    {
        auto tok = *a;
        ++a;
        if(tok.empty())
        {
            co_yield 0; // wait for more
        }
        else if(tok == "done" || tok == "fi" )
        {
            break;
        }
        else if( tok == ";" )
        {
            // skip
        }
        else if( tok == "if" )
        {
            auto if_cmd = parse_if(gen, a);
            for(auto c : if_cmd)
            {
                (void)c;
                co_yield 0;
            }
        }
        else
        {
            auto parse_cmd = parse_args(tok, gen, a);
            for(auto c : parse_cmd)
            {
                (void)c;
                co_yield 0;
            }
        }

    }
    std::cout << "-- End of Block -- " << std::endl;

    co_return;
}

Generator<WhatToDo> parse_if(Generator<std::string> & gen, Generator<std::string>::iterator & a)
{
    //auto gen = BashTokenizerGen2(in);
    //auto a = gen.begin();
    auto tok = *a;
    // parse the condition line

    std::vector<std::string> condition;

    assert("[[" == tok);
    ++a;
    while("]]" != tok)
    {
        tok = *a;
        if(tok.empty())
        {
            co_yield 0;
            continue;
        }
        condition.push_back(tok);
        ++a;
    }
    std::cout << std::format("Condition: {}", join(condition)) << std::endl;
    tok = *(++a);
    assert("then" == tok);

    auto block = parse_block(gen, a);
    for(auto c : block)
    {
        (void)c;
        co_yield 0;
    }

    co_return;
}

Generator<WhatToDo> parse_args(std::string cmd, Generator<std::string> & gen, Generator<std::string>::iterator & a_it)
{
    std::vector<std::string> args;
    args.push_back(cmd);
    while(a_it != gen.end())
    {
        auto a = *a_it;
        ++a_it;
        if(a == ";")
            break;
        if(a.empty())
        {
            co_yield 1;
            continue;
        }
        args.push_back(a);
    }
    std::cout << std::format("Executing: {}", join(args)) << std::endl;

    // we need to execute the command here and wait for

    co_return;
}

}

#endif
