#ifndef PSEUDONIX_READER_WRITER_STREAM_H
#define PSEUDONIX_READER_WRITER_STREAM_H

#include <readerwriterqueue/readerwriterqueue.h>

namespace PseudoNix
{

template<typename T>
struct ReaderWriterStream_t
{
protected:
    moodycamel::ReaderWriterQueue<T> data;
    bool _closed = false;
public:
    bool has_data() const
    {
        return data.peek() != nullptr;
    }

    size_t size_approx() const
    {
        return data.size_approx();
    }
    bool closed() const
    {
        return _closed;
    }

    bool eof() const
    {
        return !has_data() && _closed;
    }

    void close()
    {
        _closed = true;
    }

    void flush()
    {
        while(has_data())
            get();
    }

    T get()
    {
        T ch = {};
        data.try_dequeue(ch);
        return ch;
    }

    enum class Result
    {
        SUCCESS,
        EMPTY,
        END_OF_STREAM
    };

    Result check()
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
        char c;
        auto r = get(&c);
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

    ReaderWriterStream_t& operator << (ReaderWriterStream_t &ss)
    {
        while(ss.has_data())
        {
            put(ss.get());
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
        while(has_data())
        {
            ss.push_back(get());
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
        while(has_data())
        {
            s+=get();
        }
        return s;
    }
};

using ReaderWriterStream = ReaderWriterStream_t<char>;
}


#endif
