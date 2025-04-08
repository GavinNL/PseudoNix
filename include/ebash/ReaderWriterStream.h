#ifndef READER_WRITER_STREAM_H
#define READER_WRITER_STREAM_H

#include <readerwriterqueue/readerwriterqueue.h>


namespace bl
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

    bool closed() const
    {
        return _closed;
    }

    bool eof() const
    {
        if(has_data())
            return false;
        return _closed;
    }

    void close()
    {
        _closed = true;
    }

    T get()
    {
        T ch = {};
        data.try_dequeue(ch);
        return ch;
    }

    void put(T c)
    {
        data.enqueue(c);
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
            data.enqueue(ss.get());
        }
        return *this;
    }

    template<typename iter_container>
    requires std::ranges::range<iter_container>
    ReaderWriterStream_t& operator << (iter_container const &ss)
    {
        for(auto i : ss)
        {
            data.enqueue(i);
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

    //void fromStream(std::istream & out)
    //{
    //    char c;
    //    while(out.eof())
    //    {
    //        out.get(&c,1);
    //        put(c);
    //    }
    //}
    //void toStream(std::ostream & out)
    //{
    //    while(has_data()){
    //        out << get();
    //    }
    //}
};

using ReaderWriterStream = ReaderWriterStream_t<char>;
}


#endif
