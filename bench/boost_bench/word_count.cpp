// Copyright 2021, 2022 Peter Dimov.
// Distributed under the Boost Software License, Version 1.0.
// https://www.boost.org/LICENSE_1_0.txt

#define _SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING
#define _SILENCE_CXX20_CISO646_REMOVED_WARNING

//#include <boost/unordered_map.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/regex.hpp>
#ifdef ABSL_HMAP
# include "absl/container/node_hash_map.h"
# include "absl/container/flat_hash_map.h"
#endif
#include "martinus/robin_hood.h"
#include "martinus/unordered_dense.h"
#include "../hash_table8.hpp"
#include "../hash_table7.hpp"
#include "../hash_table5.hpp"

#include "./util.h"
#include "emilib/emilib3so.hpp"
#include "emilib/emilib2.hpp"

#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <string_view>
#include <string>

using namespace std::chrono_literals;

static void print_time( std::chrono::steady_clock::time_point & t1, char const* label, std::size_t s, std::size_t size )
{
    auto t2 = std::chrono::steady_clock::now();

    std::cout << label << ": " << ( t2 - t1 ) / 1ms << " ms (s=" << s << ", size=" << size << ")\n";

    t1 = t2;
}

static std::vector<std::string> words;

static void init_words()
{
#if SIZE_MAX > UINT32_MAX

    char const* fn = "enwik9"; // http://mattmahoney.net/dc/textdata

#else

    char const* fn = "enwik8"; // ditto

#endif

    auto t1 = std::chrono::steady_clock::now();

    std::ifstream is( fn );
    std::string in( std::istreambuf_iterator<char>( is ), std::istreambuf_iterator<char>{} );

    boost::regex re( "[a-zA-Z]+");
    boost::sregex_token_iterator it( in.begin(), in.end(), re, 0 ), end;

    words.assign( it, end );

    auto t2 = std::chrono::steady_clock::now();

    std::cout << fn << ": " << words.size() << " words, " << ( t2 - t1 ) / 1ms << " ms\n\n";
}

template<class Map> BOOST_NOINLINE void test_word_count( Map& map, std::chrono::steady_clock::time_point & t1 )
{
    std::size_t s = 0;

    for( auto const& word: words )
    {
        ++map[ word ];
        ++s;
    }

    print_time( t1, "Word count", s, map.size() );

    std::cout << std::endl;
}

template<class Map> BOOST_NOINLINE void test_contains( Map& map, std::chrono::steady_clock::time_point & t1 )
{
    std::size_t s = 0;

    for( auto const& word: words )
    {
        std::string_view w2( word );
        w2.remove_prefix( 1 );
#if CXX20
        s += map.count( w2 );
#else
        s += map.count( w2 );
#endif
    }

    print_time( t1, "Contains", s, map.size() );

    std::cout << std::endl;
}

template<class Map> BOOST_NOINLINE void test_count( Map& map, std::chrono::steady_clock::time_point & t1 )
{
    std::size_t s = 0;

    for( auto const& word: words )
    {
        std::string_view w2( word );
        w2.remove_prefix( 1 );

        s += map.count( w2 );
    }

    print_time( t1, "Count", s, map.size() );

    std::cout << std::endl;
}

template<class Map> BOOST_NOINLINE void test_iteration( Map& map, std::chrono::steady_clock::time_point & t1 )
{
    std::size_t max = 0;
    std::string_view word;

    for( auto const& x: map )
    {
        if( x.second > max )
        {
            word = x.first;
            max = x.second;
        }
    }

    print_time( t1, "Iterate and find max element",  max, map.size() );

    std::cout << std::endl;
}

// counting allocator

static std::size_t s_alloc_bytes = 0;
static std::size_t s_alloc_count = 0;

template<class T> struct allocator
{
    using value_type = T;

    allocator() = default;

    template<class U> allocator( allocator<U> const & ) noexcept
    {
    }

    template<class U> bool operator==( allocator<U> const & ) const noexcept
    {
        return true;
    }

    template<class U> bool operator!=( allocator<U> const& ) const noexcept
    {
        return false;
    }

    T* allocate( std::size_t n ) const
    {
        s_alloc_bytes += n * sizeof(T);
        s_alloc_count++;

        return std::allocator<T>().allocate( n );
    }

    void deallocate( T* p, std::size_t n ) const noexcept
    {
        s_alloc_bytes -= n * sizeof(T);
        s_alloc_count--;

        std::allocator<T>().deallocate( p, n );
    }
};

//

struct record
{
    std::string label_;
    long long time_;
    std::size_t bytes_;
    std::size_t count_;
};

static std::vector<record> times;

template<template<class...> class Map> BOOST_NOINLINE void test( char const* label )
{
    std::cout << label << ":\n\n";

    s_alloc_bytes = 0;
    s_alloc_count = 0;

    Map<std::string_view, std::size_t> map;

    auto t0 = std::chrono::steady_clock::now();
    auto t1 = t0;

    test_word_count( map, t1 );

    std::cout << "Memory: " << s_alloc_bytes << " bytes in " << s_alloc_count << " allocations\n\n";

    record rec = { label, 0, s_alloc_bytes, s_alloc_count };

    test_contains( map, t1 );
    test_count( map, t1 );
    test_iteration( map, t1 );

    auto tN = std::chrono::steady_clock::now();
    std::cout << "Total: " << ( tN - t0 ) / 1ms
        << " ms|load_factor = " << map.load_factor() << " \n\n";

    rec.time_ = ( tN - t0 ) / 1ms;
    times.push_back( rec );
}

// aliases using the counting allocator
#if ABSL_HASH
    #define BstrHasher absl::Hash<K>
#elif BOOST_HASH
    #define BstrHasher boost::hash<K>
#elif HOOD_HASH
    #define BstrHasher robin_hood::hash<K>
#elif CXX17
    #define BstrHasher ankerl::unordered_dense::hash<K>
#else
    #define BstrHasher std::hash<K>
#endif

template<class K, class V> using allocator_for = ::allocator< std::pair<K const, V> >;

template<class K, class V> using std_unordered_map =
    std::unordered_map<K, V, BstrHasher, std::equal_to<K>, allocator_for<K, V>>;

//template<class K, class V> using boost_unordered_map =
//    boost::unordered_map<K, V, BstrHasher, std::equal_to<K>, allocator_for<K, V>>;

template<class K, class V> using boost_unordered_flat_map =
    boost::unordered_flat_map<K, V, BstrHasher, std::equal_to<K>, allocator_for<K, V>>;

template<class K, class V> using emhash_map8 = emhash8::HashMap<K, V, BstrHasher, std::equal_to<K>>;
template<class K, class V> using emhash_map7 = emhash7::HashMap<K, V, BstrHasher, std::equal_to<K>>;
template<class K, class V> using emhash_map5 = emhash5::HashMap<K, V, BstrHasher, std::equal_to<K>>;

template<class K, class V> using martinus_flat = robin_hood::unordered_map<K, V, BstrHasher, std::equal_to<K>>;
template<class K, class V> using martinus_dense = ankerl::unordered_dense::map<K, V, BstrHasher, std::equal_to<K>>;
template<class K, class V> using emilib2_map = emilib2::HashMap<K, V, BstrHasher, std::equal_to<K>>;
template<class K, class V> using emilib3_map = emilib::HashMap<K, V, BstrHasher, std::equal_to<K>>;

#ifdef ABSL_HMAP

template<class K, class V> using absl_node_hash_map =
    absl::node_hash_map<K, V, BstrHasher, absl::container_internal::hash_default_eq<K>, allocator_for<K, V>>;

template<class K, class V> using absl_flat_hash_map =
    absl::flat_hash_map<K, V, BstrHasher, absl::container_internal::hash_default_eq<K>, allocator_for<K, V>>;

#endif

// fnv1a_hash

template<int Bits> struct fnv1a_hash_impl;

template<> struct fnv1a_hash_impl<32>
{
    std::size_t operator()( std::string_view const& s ) const
    {
        std::size_t h = 0x811C9DC5u;

        char const * first = s.data();
        char const * last = first + s.size();

        for( ; first != last; ++first )
        {
            h ^= static_cast<unsigned char>( *first );
            h *= 0x01000193ul;
        }

        return h;
    }
};

template<> struct fnv1a_hash_impl<64>
{
    std::size_t operator()( std::string_view const& s ) const
    {
        std::size_t h = 0xCBF29CE484222325ull;

        char const * first = s.data();
        char const * last = first + s.size();

        for( ; first != last; ++first )
        {
            h ^= static_cast<unsigned char>( *first );
            h *= 0x00000100000001B3ull;
        }

        return h;
    }
};

struct fnv1a_hash: fnv1a_hash_impl< std::numeric_limits<std::size_t>::digits >
{
    using is_avalanching = void;
};

template<class K, class V> using std_unordered_map_fnv1a =
std::unordered_map<K, V, fnv1a_hash, std::equal_to<K>, allocator_for<K, V>>;

//template<class K, class V> using boost_unordered_map_fnv1a =
//    boost::unordered_map<K, V, fnv1a_hash, std::equal_to<K>, allocator_for<K, V>>;

template<class K, class V> using boost_unordered_flat_map_fnv1a =
    boost::unordered_flat_map<K, V, fnv1a_hash, std::equal_to<K>, allocator_for<K, V>>;

#ifdef ABSL_HMAP

template<class K, class V> using absl_node_hash_map_fnv1a =
    absl::node_hash_map<K, V, fnv1a_hash, absl::container_internal::hash_default_eq<K>, allocator_for<K, V>>;

template<class K, class V> using absl_flat_hash_map_fnv1a =
    absl::flat_hash_map<K, V, fnv1a_hash, absl::container_internal::hash_default_eq<K>, allocator_for<K, V>>;

#endif

//

int main()
{
    init_words();

//    test<std_unordered_map>( "std::unordered_map" );
//    test<boost_unordered_map>( "boost::unordered_map" );

    test<emhash_map8>( "emhash8::hash_map" );
    test<emhash_map7>( "emhash7::hash_map" );
    test<emhash_map5>( "emhash5::hash_map" );
    test<martinus_dense>("martinus::dense_hash_map" );
    test<martinus_flat>("martinus::flat_hash_map" );

    test<emilib2_map> ("emilib2_map" );
    test<emilib3_map> ("emilib3_map" );
    test<boost_unordered_flat_map>( "boost::unordered_flat_map" );

#ifdef ABSL_HMAP

    test<absl_node_hash_map>( "absl::node_hash_map" );
    test<absl_flat_hash_map>( "absl::flat_hash_map" );

#endif

    test<std_unordered_map_fnv1a>( "std::unordered_map, FNV-1a" );
//    test<boost_unordered_map_fnv1a>( "boost::unordered_map, FNV-1a" );
    test<boost_unordered_flat_map_fnv1a>( "boost::unordered_flat_map, FNV-1a" );

#ifdef ABSL_HMAP

    test<absl_node_hash_map_fnv1a>( "absl::node_hash_map, FNV-1a" );
    test<absl_flat_hash_map_fnv1a>( "absl::flat_hash_map, FNV-1a" );

#endif

    std::cout << "---\n\n";

    for( auto const& x: times )
    {
        std::cout << std::setw( 35 ) << ( x.label_ + ": " ) << std::setw( 5 ) << x.time_ << " ms, " << std::setw( 9 ) << x.bytes_ << " bytes in " << x.count_ << " allocations\n";
    }
}

#if 0
# include "absl/container/internal/raw_hash_set.cc"
# include "absl/hash/internal/hash.cc"
# include "absl/hash/internal/low_level_hash.cc"
# include "absl/hash/internal/city.cc"
#endif
