#ifndef PSEUDONIX_READER_WRITER_STREAM_H
#define PSEUDONIX_READER_WRITER_STREAM_H

#include <readerwriterqueue/readerwriterqueue.h>
#include <mutex>

namespace PseudoNix
{

template<typename T>
struct ReaderWriterStream_t
{
protected:
    moodycamel::ReaderWriterQueue<T> data;
    std::mutex _m;
public:
    std::lock_guard<std::mutex> lock()
    {
        return std::lock_guard(_m);
    }
    bool has_data() const
    {
        return data.peek() != nullptr;
    }

    size_t size_approx() const
    {
        return data.size_approx();
    }

    bool eof() const
    {
        return !has_data() && _eof;
    }

    void flush()
    {
        while(has_data())
            get();
    }

    enum class Result
    {
        SUCCESS,
        EMPTY,
        END_OF_STREAM
    };

    Result check() const
    {
        if(auto front = data.peek())
        {
            return Result::SUCCESS;
        }
        else
        {
            if(_eof)
            {
                return Result::END_OF_STREAM;
            }
        }
        return Result::EMPTY;
    }
    Result get(char *c)
    {
        if(auto front = data.peek())
        {
            *c = *front;
            data.pop();
            return Result::SUCCESS;
        }
        else
        {
            if(_eof)
            {
                _eof = false;
                return Result::END_OF_STREAM;
            }
        }
        return Result::EMPTY;
    }

    Result read_line(std::string & line)
    {
        char c = 0;
        auto r = get(&c);
        line.clear();
        while(true)
        {
            switch(r)
            {
                case Result::SUCCESS:
                    line.push_back(c);
                    if(line.back()=='\n')
                    {
                        line.pop_back();
                        return Result::SUCCESS;
                    }
                    break;
                case Result::END_OF_STREAM: return r;
                case Result::EMPTY: return Result::SUCCESS;
            }
            r = get(&c);
        }
    }

    void put(T c)
    {
        data.enqueue(c);
    }

    bool _eof = false;
    void set_eof()
    {
        _eof = true;
    }

    ReaderWriterStream_t(){}

    template<typename iter_container>
    requires std::ranges::range<iter_container>
    ReaderWriterStream_t(iter_container const & d)
    {
        for(auto i : d)
        {
            data.enqueue(i);
        }
    }

    ReaderWriterStream_t& operator << (ReaderWriterStream_t &ss)
    {
        char c = 0;
        while(ss.get(&c) == Result::SUCCESS)
        {
            put(c);
        }
        return *this;
    }

    ReaderWriterStream_t& operator << (char const *s)
    {
        while(*s)
        {
            put(*s);
            ++s;
        }
        return *this;
    }

    template<typename iter_container>
    requires std::ranges::range<iter_container>
    ReaderWriterStream_t& operator << (iter_container const &ss)
    {
        for(auto i : ss)
        {
            put(i);
        }
        return *this;
    }

    template<typename iter_container>
        requires std::ranges::range<iter_container>
    ReaderWriterStream_t& operator >> (iter_container &ss)
    {
        char c = 0;
        while(get(&c) == Result::SUCCESS)
        {
            ss.push_back(c);
        }
        return *this;
    }

    ReaderWriterStream_t& operator << (T const& d)
    {
        data.enqueue(d);
        return *this;
    }

    std::string str()
    {
        std::string s;
        char c=0;
        while(get(&c) == Result::SUCCESS)
        {
            s.push_back(c);
        }
        return s;
    }
};

using ReaderWriterStream = ReaderWriterStream_t<char>;
}


#endif
