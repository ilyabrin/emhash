#include "util.h"
#include <sstream>

#ifndef _WIN32
#include <sys/resource.h>
#endif

//#include "wyhash.h"

//#define EMH_STATIS 1
//#define ET            1
//#define EMH_WYHASH64   1
//#define HOOD_HASH     1
//
//#define EMH_PACK_TAIL 16
//#define EMH_HIGH_LOAD 123456

#if CK_HMAP
#include "ck/Common/HashTable/HashMap.h"
#endif

#include "../hash_table5.hpp"
#include "../hash_table6.hpp"
#include "../hash_table7.hpp"
#include "../hash_table8.hpp"

//#include "../thirdparty/emhash/hash_table8v.hpp"
//#include "../thirdparty/emhash/hash_table8v2.hpp"

#ifdef HAVE_BOOST
#include <boost/unordered/unordered_flat_map.hpp>
#endif

#if X86
    #include "emilib/emilib2s.hpp"
    #include "emilib/emilib3so.hpp"
    #include "emilib/emilib2o.hpp"
#endif

#include "martinus/robin_hood.h"

#if CXX17
#include "martinus/unordered_dense.h"
#endif

#if ET
    #include "phmap/phmap.h"
    #include "tsl/robin_map.h"
#if X86_64
    #include "ska/flat_hash_map.hpp"
    #include "hrd/hash_set_m.h"
#endif
#endif

#if FOLLY_F14
#include "folly/container/F14Map.h"
#endif

static const auto RND = getus() + time(0);
static float max_lf = 0.80;

static std::map<std::string, std::string> show_name =
{
   {"emhash7", "emhash7"},
   {"emhash8", "emhash8"},
   {"emhash5", "emhash5"},
#if X86
//    {"emilib", "emilib"},
    {"emilib2", "emilib2"},
    {"emilib3", "emilib3"},
    {"hrd_m", "hrdm"},
#endif

#if HAVE_BOOST
    {"boost",  "boost flat"},
#endif
#if CK_HMAP
    {"HashMapCell",  "ck_hashmap"},
    {"HashMapTable", "ck_hashmap"},
#endif

    {"ankerl", "martinus dense"},

#if QC_HASH
    {"qc", "qchash"},
    {"fph", "fph"},
#endif

     {"emhash6", "emhash6"},
#if ABSL_HMAP
    {"absl", "absl flat"},
#endif
#if CXX20
    {"rigtorp", "rigtorp"},
    {"jg", "jg_dense"},
#endif
#if ET
    {"phmap", "phmap flat"},
    {"robin_hood", "martinus flat"},
//    {"folly", "f14_vector"},

#if ET > 1
    {"robin_map", "tessil robin"},
    {"ska", "skarupk flat"},
#endif
#endif
};

static const char* find_hash(const std::string& map_name)
{
    if (map_name.find("emilib2") < 10)
        return show_name.count("emilib2") ? show_name["emilib2"].data() : nullptr;
    if (map_name.find("emilib3") < 10)
        return show_name.count("emilib3") ? show_name["emilib3"].data() : nullptr;
    if (map_name.find("HashMapCell") < 30)
        return show_name.count("HashMapCell") ? show_name["HashMapCell"].data() : nullptr;
    if (map_name.find("HashMapTable") < 30)
        return show_name.count("HashMapTable") ? show_name["HashMapTable"].data() : nullptr;

    for (const auto& kv : show_name)
    {
        if (map_name.find(kv.first) < 10)
            return kv.second.c_str();
    }

    return nullptr;
}

#ifndef RT
    #define RT 1 //2 wyrand 1 sfc64 3 RomuDuoJr 4 Lehmer64 5 mt19937_64
#endif

#if RT == 1
    #define MRNG sfc64
#elif RT == 2
    #define MRNG WyRand
#elif RT == 3
    #define MRNG RomuDuoJr
#else
    #define MRNG Lehmer64
#endif

// this is probably the fastest high quality 64bit random number generator that exists.
// Implements Small Fast Counting v4 RNG from PractRand.
class sfc64 {
    public:
        using result_type = uint64_t;

        // no copy ctors so we don't accidentally get the same random again
        sfc64(sfc64 const&) = delete;
        sfc64& operator=(sfc64 const&) = delete;

        sfc64(sfc64&&) = default;
        sfc64& operator=(sfc64&&) = default;

        sfc64(std::array<uint64_t, 4> const& state)
            : m_a(state[0])
              , m_b(state[1])
              , m_c(state[2])
              , m_counter(state[3]) {}

        static constexpr uint64_t(min)() {
            return (std::numeric_limits<uint64_t>::min)();
        }
        static constexpr uint64_t(max)() {
            return (std::numeric_limits<uint64_t>::max)();
        }

        sfc64()
            : sfc64(UINT64_C(0x853c49e6748fea9b)) {}

        sfc64(uint64_t seed)
            : m_a(seed), m_b(seed), m_c(seed), m_counter(1) {
                for (int i = 0; i < 12; ++i) {
                    operator()();
                }
            }

        void seed() {
            *this = sfc64{std::random_device{}()};
        }

        uint64_t operator()() noexcept {
            auto const tmp = m_a + m_b + m_counter++;
            m_a = m_b ^ (m_b >> right_shift);
            m_b = m_c + (m_c << left_shift);
            m_c = rotl(m_c, rotation) + tmp;
            return tmp;
        }

        // this is a bit biased, but for our use case that's not important.
        uint64_t operator()(uint64_t boundExcluded) noexcept {
#ifdef __SIZEOF_INT128__
            return static_cast<uint64_t>((static_cast<unsigned __int128>(operator()()) * static_cast<unsigned __int128>(boundExcluded)) >> 64u);
#elif _WIN32
            uint64_t high;
            uint64_t a = operator()();
            _umul128(a, boundExcluded, &high);
            return high;
#endif
        }

        std::array<uint64_t, 4> state() const {
            return {m_a, m_b, m_c, m_counter};
        }

        void state(std::array<uint64_t, 4> const& s) {
            m_a = s[0];
            m_b = s[1];
            m_c = s[2];
            m_counter = s[3];
        }

    private:
        template <typename T>
            T rotl(T const x, int k) {
                return (x << k) | (x >> (8 * sizeof(T) - k));
            }

        static constexpr int rotation = 24;
        static constexpr int right_shift = 11;
        static constexpr int left_shift = 3;
        uint64_t m_a;
        uint64_t m_b;
        uint64_t m_c;
        uint64_t m_counter;
};

static inline float now2sec()
{
#if _WIN320
    FILETIME ptime[4];
    GetThreadTimes(GetCurrentThread(), &ptime[0], &ptime[2], &ptime[2], &ptime[3]);
    return (ptime[3].dwLowDateTime + ptime[2].dwLowDateTime) / 10000000.0f;
#elif __linux__
    struct rusage rup;
    getrusage(RUSAGE_SELF, &rup);
    long sec  = rup.ru_utime.tv_sec  + rup.ru_stime.tv_sec;
    long usec = rup.ru_utime.tv_usec + rup.ru_stime.tv_usec;
    return sec + usec / 1000000.0;
#elif __unix__
    struct timeval start;
    gettimeofday(&start, NULL);
    return start.tv_sec + start.tv_usec / 1000000.0;
#else
    auto tp = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(tp).count() / 1000000.0f;
#endif
}

template<class MAP>
static void bench_insert(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("    %s\n", map_name);

#if X86_64
    size_t maxn = 1000000;
#else
    size_t maxn = 1000000 / 5;
#endif

    map.max_load_factor(max_lf);
    for (int  i = 0; i < 3; i++) {
        auto nows = now2sec();
        {
            {
                auto ts = now2sec();
                MRNG rng(RND + 15 + i);
                for (size_t n = 0; n < maxn; ++n)
                    map[static_cast<int>(rng())];
                printf("\t(lf=%.2f) insert %.2f",  map.load_factor(), now2sec() - ts);
                fflush(stdout);
            }
            {
                auto ts = now2sec();
                MRNG rng(RND + 15 + i);
                for (size_t n = 0; n < maxn; ++n)
                    map.erase(static_cast<int>(rng()));
                printf(", remove %.2f", now2sec() - ts);
                fflush(stdout);
                assert(map.size() == 0);
            }
            {
                auto ts = now2sec();
                MRNG rng(RND + 16 + i);
                for (size_t n = 0; n < maxn; ++n)
                    map.emplace(static_cast<int>(rng()), 0);
                printf(", reinsert %.2f", now2sec() - ts);
            }
            {
                auto ts = now2sec();
                map.clear();
                printf(", clear %.3f", now2sec() - ts);
            }
        }
        printf(" total %dM int time = %.2f s\n", int(maxn / 1000000), now2sec() - nows);
        maxn *= 10;
    }
}

template<class MAP>
static void bench_AccidentallyQuadratic(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("    %s", map_name);

    auto nows = now2sec();
    sfc64 rng(12345);

    for (size_t n = 0; n < 10'000'000; ++n) {
        map[static_cast<int>(rng())];
    }
    assert(9988513 == map.size());

    //bench.beginMeasure("iterate");
    uint64_t sum = 0;
    for (auto const& kv : map) {
        sum += kv.first;
        sum += kv.second;
    }
    if (sum != UINT64_C(18446739465311920326))
		puts("error\n");

//    bench.beginMeasure("iterate & copy");
    MAP map2;
    for (auto const& kv : map) {
        map2[kv.first] = kv.second;
    }
    assert(map.size() == map2.size());
    printf(" time use %.2f s\n", now2sec() - nows);
}

template <typename T>
struct as_bits_t {
    T value;
};

template <typename T>
as_bits_t<T> as_bits(T value) {
    return as_bits_t<T>{value};
}

template <typename T>
std::ostream& operator<<(std::ostream& os, as_bits_t<T> const& t) {
    os << std::bitset<sizeof(t.value) * 8>(t.value);
    return os;
}

template<class RandomIt, class URBG>
static void rshuffle(RandomIt first, RandomIt last, URBG&& g)
{
    typedef typename std::iterator_traits<RandomIt>::difference_type diff_t;
    typedef std::uniform_int_distribution<diff_t> distr_t;
    typedef typename distr_t::param_type param_t;

    distr_t D;
    diff_t n = last - first;
    for (diff_t i = n-1; i > 0; --i) {
        using std::swap;
        swap(first[i], first[D(g, param_t(0, i))]);
    }
}

template<class ForwardIt, class T>
static void iotas(ForwardIt first, ForwardIt last, T value)
{
    while(first != last) {
        *first++ = value;
        ++value;
    }
}

template<class MAP>
static void bench_randomInsertErase(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);
    auto nows = now2sec();

    {
        uint32_t min_n    = 1 << 20;
        uint32_t max_loop = min_n << 5;
        map.max_load_factor(max_lf);
        for (int j = 0; j < 5; ++j) {
            MRNG rng(RND + 6 + j);
            MRNG rng2(RND + 6 + j);
            // each iteration, set 4 new random bits.
            // std::cout << (i + 1) << ". " << as_bits(bitMask) << std::endl;
            auto maxn = min_n * (50 + j * 9) / 100;
            for (size_t i = 0; i < maxn; ++i) {
                map.emplace(rng(), 0);
            }

            //auto ts = now2sec();
            maxn = max_loop * 10 / (10 + 4*j);
            // benchmark randomly inserting & erasing
            for (size_t i = 0; i < maxn; ++i) {
                map.emplace(rng(), 0);
                map.erase(rng2());
            }
//            printf("    %8u %2d M cycles time %.3f s map size %8d loadf = %.2f\n",
//                    maxn, int(min_n / 1000000), now2sec() - ts, (int)map.size(), map.load_factor());
            min_n *= 2;
            map.clear();
        }
    }

    {
        MAP map2;
        map2.max_load_factor(max_lf);
        std::vector<int> bits(64, 0);
        iotas(bits.begin(), bits.end(), 0);
        sfc64 rng(999);

#if 0
        for (auto &v : bits) v = rng();
#else
        rshuffle(bits.begin(), bits.end(), rng);
#endif

        uint64_t bitMask = 0;
        auto bitsIt = bits.begin();
        //size_t const expectedFinalSizes[] = {7, 127, 2084, 32722, 524149, 8367491};
        size_t const max_n = 50000000;

        for (int i = 0; i < 6; ++i) {
            for (int b = 0; b < 4; ++b) {
                bitMask |= UINT64_C(1) << *bitsIt++;
            }

//            auto ts = now2sec();
            for (size_t i = 0; i < max_n; ++i) {
                map2.emplace(rng() & bitMask, 0);
                map2.erase(rng() & bitMask);
            }
//            printf("    %02d bits  %2d M cycles time %.3f s map size %d loadf = %.2f\n",
//                    int(std::bitset<64>(bitMask).count()), int(max_n / 1000000), now2sec() - ts, (int)map2.size(), map2.load_factor());
        }
    }

    printf(" total time = %.2f s\n", now2sec() - nows);
}

template<class MAP>
static void bench_randomDistinct2(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

#if X86_64
    constexpr size_t const n = 50000000;
#else
    constexpr size_t const n = 50000000 / 2;
#endif
    auto nows = now2sec();
    MRNG rng(RND + 100);

    map.max_load_factor(max_lf);
    int checksum;
    {
        //auto ts = now2sec();
        checksum = 0;
        size_t const max_rng = n / 20;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng(max_rng))];
        }
//        printf("     05%% distinct %.3f s loadf = %.2f, size = %d\n", now2sec() - ts, map.load_factor(), (int)map.size());
        assert(RND != 123 || 549985352 == checksum);
    }

    {
        map.clear();
        //auto ts = now2sec();
        checksum = 0;
        size_t const max_rng = n / 4;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng(max_rng))];
        }
//        printf("     25%% distinct %.3f s loadf = %.2f, size = %d\n", now2sec() - ts, map.load_factor(), (int)map.size());
        assert(RND != 123 || 149979034 == checksum);
    }

    {
        map.clear();
        //auto ts = now2sec();
        size_t const max_rng = n / 2;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng(max_rng))];
        }
//        printf("     50%% distinct %.3f s loadf = %.2f, size = %d\n", now2sec() - ts, map.load_factor(), (int)map.size());
        assert(RND != 123 || 249981806 == checksum);
    }

    {
        map.clear();
        //auto ts = now2sec();
        checksum = 0;
        for (size_t i = 0; i < n; ++i) {
            checksum += ++map[static_cast<int>(rng())];
        }
//        printf("    100%% distinct %.3f s loadf = %.2f, size = %d\n", now2sec() - ts, map.load_factor(), (int)map.size());
        assert(RND != 123 || 50291811 == checksum);
    }
    //#endif

    printf(" total time = %.2f s\n", now2sec() - nows);
}

#define CODE_FOR_NUCLEOTIDE(nucleotide) (" \0 \1\3  \2"[nucleotide & 0x7])

template<class Map>
static size_t kcount(const std::vector<char> &poly, const std::string &oligo) {

    Map map;
    //map.max_load_factor(0.5);

    uint64_t key = 0;
    const uint64_t mask = ((uint64_t)1 << 2 * oligo.size()) - 1;

    // For the first several nucleotides we only need to append them to key in
    // preparation for the insertion of complete oligonucleotides to map.
    for (size_t i = 0; i < oligo.size() - 1; ++i)
        key = (key << 2 & mask) | poly[i];

    // Add all the complete oligonucleotides of oligo.size() to
    // map and update the count for each oligonucleotide.
    for (size_t i = oligo.size() - 1; i < poly.size(); ++i){
        key= (key << 2 & mask) | poly[i];
        ++map[key];
    }

    // Generate the key for oligonucleotide.
    key = 0;
    for (size_t i = 0; i < oligo.size(); ++i) {
        key = (key << 2) | CODE_FOR_NUCLEOTIDE(oligo[i]);
    }

    if (oligo == "GGT")
        printf(" (lf=%.2f) ", map.load_factor());

    return map[key];
}

static int state = 42;
static inline int fasta_next() {
    static constexpr int IM = 139968, IA = 3877, IC = 29573;

    state = (state * IA + IC) % IM;
    float p = state * (1.0f / IM);
    return (p >= 0.3029549426680f) + (p >= 0.5009432431601f) + (p >= 0.6984905497992f);
}

template<class MAP>
static void bench_knucleotide(MAP& map) {
    static constexpr size_t n = 25000000;

    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

    state = 42;
    for (size_t i = 0; i < n * 3; ++i)
        (void)fasta_next();

    std::vector<char> poly(n * 5);
    for (size_t i = 0; i < poly.size(); ++i) {
        poly[i] = fasta_next();
    }

    auto nows = now2sec();
    int ans = 0;
    ans += kcount<MAP>(poly, "GGTATTTTAATTTATAGT");
    ans += kcount<MAP>(poly, "GGTATTTTAATT");
    ans += kcount<MAP>(poly, "GGTATT");
    ans += kcount<MAP>(poly, "GGTA");
    ans += kcount<MAP>(poly, "GGT");
    printf(" ans = %d time = %.2f s\n", ans, now2sec() - nows);
}

class vec2 {
    uint32_t m_xy;

public:
    constexpr vec2(uint16_t x, uint16_t y)
        : m_xy{static_cast<uint32_t>(x) << 16U | y} {}

    constexpr explicit vec2(uint32_t xy)
        : m_xy(xy) {}

    [[nodiscard]] constexpr auto pack() const -> uint32_t {
        return m_xy;
    };

    [[nodiscard]] constexpr auto add_x(uint16_t x) const -> vec2 {
        return vec2{m_xy + (static_cast<uint32_t>(x) << 16U)};
    }

    [[nodiscard]] constexpr auto add_y(uint16_t y) const -> vec2 {
        return vec2{(m_xy & 0xffff0000) | ((m_xy + y) & 0xffff)};
    }

    template <typename Op>
    constexpr void for_each_surrounding(Op&& op) const {
        uint32_t v = m_xy;

        uint32_t upper = (v & 0xffff0000U);
        uint32_t l1 = (v - 1) & 0xffffU;
        uint32_t l2 = v & 0xffffU;
        uint32_t l3 = (v + 1) & 0xffffU;

        op((upper - 0x10000) | l1);
        op((upper - 0x10000) | l2);
        op((upper - 0x10000) | l3);

        op(upper | l1);
        // op(upper | l2);
        op(upper | l3);

        op((upper + 0x10000) | l1);
        op((upper + 0x10000) | l2);
        op((upper + 0x10000) | l3);
    }
};

template <typename M>
static void game_of_life(const char* name, size_t nsteps, size_t finalPopulation, M& map1, std::vector<vec2> state) {

    map1.clear();
    auto map2 = map1;

    for (auto& v : state) {
        v = v.add_x(UINT16_MAX / 2).add_y(UINT16_MAX / 2);
        map1[v.pack()] = true;
        v.for_each_surrounding([&](uint32_t xy) { map1.emplace(xy, false); });
    }

    auto* m1 = &map1;
    auto* m2 = &map2;
    for (size_t i = 0; i < nsteps; ++i) {
        for (auto const kv : *m1) {
            auto const& pos = kv.first;
            auto alive = kv.second;
            int neighbors = 0;
            vec2{pos}.for_each_surrounding([&](uint32_t xy) {
                if (auto x = m1->find(xy); x != m1->end()) {
                    neighbors += x->second;
                }
            });
            if ((alive && (neighbors == 2 || neighbors == 3)) || (!alive && neighbors == 3)) {
                (*m2)[pos] = true;
                vec2{pos}.for_each_surrounding([&](uint32_t xy) { m2->emplace(xy, false); });
            }
        }
        m1->clear();
        std::swap(m1, m2);
    }

    size_t count = 0;
    for (auto const kv : *m1) {
        count += kv.second;
    }
    assert(finalPopulation ==count);
}

template<class MAP>
static void bench_GameOfLife(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

    auto stastabilizing = now2sec();
    {
        // https://conwaylife.com/wiki/R-pentomino
        game_of_life( "R-pentomino", 1103, 116, map, {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}});

        // https://conwaylife.com/wiki/Acorn
        game_of_life( "Acorn", 5206, 633, map, {{1, 0}, {3, 1}, {0, 2}, {1, 2}, {4, 2}, {5, 2}, {6, 2}});

        // https://conwaylife.com/wiki/Jaydot
        game_of_life( "Jaydot", 6929, 1124, map, {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 3}, {1, 4}, {2, 4}, {0, 5}});

        // https://conwaylife.com/wiki/Bunnies
        game_of_life( "Bunnies", 17332, 1744, map, {{0, 0}, {6, 0}, {2, 1}, {6, 1}, {2, 2}, {5, 2}, {7, 2}, {1, 3}, {3, 3}});

        printf(" stastabilizing = %.2f", now2sec() - stastabilizing);
    }

    auto grow = now2sec();
    {
        auto map1 = map;
        // https://conwaylife.com/wiki/Gotts_dots
        game_of_life( "Gotts dots", 2000, 4599, map1,
        {
                {0, 0},    {0, 1},    {0, 2},                                                                                 // 1
                {4, 11},   {5, 12},   {6, 13},   {7, 12},   {8, 11},                                                          // 2
                {9, 13},   {9, 14},   {9, 15},                                                                                // 3
                {185, 24}, {186, 25}, {186, 26}, {186, 27}, {185, 27}, {184, 27}, {183, 27}, {182, 26},                       // 4
                {179, 28}, {180, 29}, {181, 29}, {179, 30},                                                                   // 5
                {182, 32}, {183, 31}, {184, 31}, {185, 31}, {186, 31}, {186, 32}, {186, 33}, {185, 34},                       // 6
                {175, 35}, {176, 36}, {170, 37}, {176, 37}, {171, 38}, {172, 38}, {173, 38}, {174, 38}, {175, 38}, {176, 38}, // 7
        });

        // https://conwaylife.com/wiki/Puffer_2
        game_of_life( "Puffer 2", 2000, 7400, map1,
        {
                {1, 0}, {2, 0}, {3, 0},  {15, 0}, {16, 0}, {17, 0}, // line 0
                {0, 1}, {3, 1}, {14, 1}, {17, 1},                   // line 1
                {3, 2}, {8, 2}, {9, 2},  {10, 2}, {17, 2},          // line 2
                {3, 3}, {8, 3}, {11, 3}, {17, 3},                   // line 3
                {2, 4}, {7, 4}, {16, 4},                            // line 4
        });
    }

    printf(", grow = %.2f (total %.2f) time s\n", now2sec() - grow, now2sec() - stastabilizing);
}

template<class MAP>
static void bench_copy(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

    size_t result = 0;
    sfc64 rng(987);

    constexpr int KN = 1000;
    constexpr int KL = 1000;
    MAP mapSource(1'000);
//    mapSource.max_load_factor(max_lf);
    uint64_t rememberKey = 0;
    for (size_t i = 0; i < 200'000; ++i) {
        auto key = rng();
        if (i == 100'000) {
            rememberKey = key;
        }
        mapSource[key] = i;
    }

    auto nows = now2sec();
    MAP mapForCopy = mapSource;
    for (size_t n = 0; n < KL; ++n) {
        MAP m = mapForCopy;
        result += m.size() + m[rememberKey];
        for (int i = 0; i < KN; i++) //with different load factor
            mapForCopy[rng()] = rng();
    }
//    assert(result == 300019900);
    auto copyt = now2sec();
    printf(" copy = %.2f", copyt - nows);

    mapForCopy = mapSource;
    MAP m;
    for (size_t n = 0; n < KL; ++n) {
        m = mapForCopy;
        result += m.size() + m[rememberKey];
        for (int i = 0; i < KN; i++)
            mapForCopy[rng()] = rng();
    }
//    assert(result == 600039800);
    printf(", assign time = %.2f s, result = %ld\n", now2sec() - copyt, result);
}

template<class MAP>
static size_t runInsertEraseString(size_t max_n, size_t string_length, uint32_t bitMask)
{
    //printf("%s map = %s\n", __FUNCTION__, typeid(MAP).name());
    MRNG rng(RND + 4);

    // time measured part
    size_t verifier = 0;
    std::stringstream ss;
    ss << string_length << " bytes" << std::dec;

    std::string str(string_length, 'x');
    // point to the back of the string (32bit aligned), so comparison takes a while
    auto const idx32 = (string_length / 4) - 1;
    auto const strData32 = reinterpret_cast<uint32_t*>(&str[0]) + idx32;

    MAP map;
    map.max_load_factor(max_lf);

//    auto ts = now2sec();
    for (size_t i = 0; i < max_n; ++i) {
        *strData32 = rng() & bitMask;
#if 0
        // create an entry.
        map[str] = 0;
        *strData32 = rng() & bitMask;
        auto it = map.find(str);
        if (it != map.end()) {
            ++verifier;
            map.erase(it);
        }
#else
        map.emplace(str, 0);
        *strData32 = rng() & bitMask;
        verifier += map.erase(str);
#endif
    }

//    printf("%4zd bytes time = %.2f, loadf = %.2f %d\n", string_length, now2sec() - ts, map.load_factor(), (int)map.size());
    return verifier;
}

template<class MAP>
static uint64_t randomFindInternalString(size_t numRandom, size_t const length, size_t numInserts, size_t numFindsPerInsert)
{
    size_t constexpr NumTotal = 4;
    size_t const numSequential = NumTotal - numRandom;

    size_t const numFindsPerIter = numFindsPerInsert * NumTotal;

    std::stringstream ss;
    ss << (numSequential * 100 / NumTotal) << "% " << length << " byte";
    auto title = ss.str();

    sfc64 rng(RND + 3);

    size_t num_found = 0;

    std::array<bool, NumTotal> insertRandom = {false};
    for (size_t i = 0; i < numRandom; ++i) {
        insertRandom[i] = true;
    }

    sfc64 anotherUnrelatedRng(987654321);
    auto const anotherUnrelatedRngInitialState = anotherUnrelatedRng.state();
    sfc64 findRng(anotherUnrelatedRngInitialState);

    std::string str(length, 'y');
    // point to the back of the string (32bit aligned), so comparison takes a while
    auto const idx32 = (length / 4) - 1;
    auto const strData32 = reinterpret_cast<uint32_t*>(&str[0]) + idx32;

    auto ts = now2sec();
    MAP map;
    map.max_load_factor(max_lf);
    {
        size_t i = 0;
        size_t findCount = 0;

        do {
            // insert NumTotal entries: some random, some sequential.
            std::shuffle(insertRandom.begin(), insertRandom.end(), rng);
            for (bool isRandomToInsert : insertRandom) {
                auto val = anotherUnrelatedRng();
                if (isRandomToInsert) {
                    *strData32 = (uint32_t)rng();
                } else {
                    *strData32 = uint32_t(val);
                }
                map[str] = 1;
                ++i;
            }

            // the actual benchmark code which sohould be as fast as possible
            for (size_t j = 0; j < numFindsPerIter; ++j) {
                if (++findCount > i) {
                    findCount = 0;
                    findRng.state(anotherUnrelatedRngInitialState);
                }
                *strData32 = (uint32_t)findRng();
                auto it = map.find(str);
                if (it != map.end()) {
#ifndef CK_HMAP
                    num_found += it->second;
#else
                    num_found += 1;
#endif
                }
            }
        } while (i < numInserts);
    }

    if (map.size() > 12)
    printf("    %s success time = %.2f s %8d loadf = %.2f\n",
            title.c_str(), now2sec() - ts, (int)num_found, map.load_factor());
    return num_found;
}

template<class MAP>
static void bench_randomFindString(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s\n", map_name);

    auto nows = now2sec();
    auto now1 = nows, now2 = nows;
    if (1)
    {
        static constexpr size_t numInserts = 1000000 / 2;
        static constexpr size_t numFindsPerInsert = 200 / 2;

        randomFindInternalString<MAP>(4, 13, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(3, 13, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(2, 13, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(1, 13, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(0, 13, numInserts, numFindsPerInsert);
        now1 = now2sec();
    }

    {
        static constexpr size_t numInserts = 100000;
        static constexpr size_t numFindsPerInsert = 1000;

        randomFindInternalString<MAP>(4, 100, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(3, 100, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(2, 100, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(1, 100, numInserts, numFindsPerInsert);
        randomFindInternalString<MAP>(0, 100, numInserts, numFindsPerInsert);
        now2 = now2sec();
    }
    printf("total time = %.2f + %.2f = %.2f s\n", now1 - nows, now2 - now1, now2 - nows);
}

template<class MAP>
static void bench_randomEraseString(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

    auto nows = now2sec();
    { runInsertEraseString<MAP>(20000000, 7, 0xfffff); }
    { runInsertEraseString<MAP>(20000000, 8, 0xfffff); }
    { runInsertEraseString<MAP>(20000000, 13, 0xfffff); }
    { runInsertEraseString<MAP>(10000000, 24, 0xfffff); }
    { runInsertEraseString<MAP>(12000000, 100, 0x4ffff); }
    { runInsertEraseString<MAP>(8000000,  200, 0x3ffff); }
    { runInsertEraseString<MAP>(6000000,  1000,0x7ffff); }

    printf(" total time = %.2f s\n", now2sec() - nows);
}

template<class MAP>
static uint64_t randomFindInternal(size_t numRandom, uint64_t bitMask, const size_t numInserts, const size_t numFindsPerInsert) {
    size_t constexpr NumTotal = 4;
    size_t const numSequential = NumTotal - numRandom;

    size_t const numFindsPerIter = numFindsPerInsert * NumTotal;

    sfc64 rng(RND + 2);

    size_t num_found = 0;
    MAP map;
    map.max_load_factor(max_lf);
    std::array<bool, NumTotal> insertRandom = {false};
    for (size_t i = 0; i < numRandom; ++i) {
        insertRandom[i] = true;
    }

    sfc64 anotherUnrelatedRng(987654321);
    auto const anotherUnrelatedRngInitialState = anotherUnrelatedRng.state();
    sfc64 findRng(anotherUnrelatedRngInitialState);
    auto ts = now2sec();

    {
        size_t i = 0;
        size_t findCount = 0;

        //bench.beginMeasure(title.c_str());
        do {
            // insert NumTotal entries: some random, some sequential.
            std::shuffle(insertRandom.begin(), insertRandom.end(), rng);
            for (bool isRandomToInsert : insertRandom) {
                const auto val = anotherUnrelatedRng();
                if (isRandomToInsert) {
                    map[rng() & bitMask] = static_cast<size_t>(1);
                } else {
                    map[val & bitMask] = static_cast<size_t>(1);
                }
            }
            i += insertRandom.size();

            // the actual benchmark code which sohould be as fast as possible
            for (size_t j = 0; j < numFindsPerIter; ++j) {
                if (++findCount > i) {
                    findCount = 0;
                    findRng.state(anotherUnrelatedRngInitialState);
                }
                num_found += map.count(findRng() & bitMask);
            }
        } while (i < numInserts);
    }

    if (map.size() == 0) {
#if _WIN32
    printf("    %3u%% %016llx time = %.2f s, %8d loadf = %.2f\n",
#else
    printf("    %3u%% %016lx time = %.2f s, %8d loadf = %.2f\n",
#endif
            uint32_t(numSequential * 100 / NumTotal), bitMask, now2sec() - ts, (int)num_found, map.load_factor());
    }

    return map.size();
}

template<class MAP>
static void bench_IterateIntegers(MAP& map)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

    size_t const num_iters = 50000;
    uint64_t result = 0;
    auto ts = now2sec();

    {
        MRNG rng(123);
        for (size_t n = 0; n < num_iters; ++n) {
            map[rng()] = n;
            for (const auto & keyVal : map)
#ifndef CK_HMAP
                result += keyVal.second;
#else
                result += 1;
#endif
        }
        assert(result == 20833333325000ull);
    }

    auto ts1 = now2sec();
    {
        MRNG rng(123);
        for (size_t n = 0; n < num_iters; ++n) {
            map.erase(rng());
            for (auto const& keyVal : map)
#ifndef CK_HMAP
                result += keyVal.second;
#else
                result += 1;
#endif
        }
    }
    assert(result == 62498750000000ull + 20833333325000ull);
    printf(", add/removing time = %.2f, %.2f|%lu\n", (ts1 - ts), now2sec() - ts1, result);
}

template<class MAP>
static void bench_randomFind(MAP& bench, size_t numInserts, size_t numFindsPerInsert)
{
    auto map_name = find_hash(typeid(MAP).name());
    if (!map_name)
        return;
    printf("\t%20s", map_name);

    static constexpr auto lower32bit = UINT64_C(0x00000000FFFFFFFF);
    static constexpr auto upper32bit = UINT64_C(0xFFFFFFFF00000000);
    static constexpr auto mediu32bit = UINT64_C(0x0000FFFFFFFF0000);

    auto ts = now2sec();
    uint64_t sum = 0;

    sum += randomFindInternal<MAP>(4, lower32bit, numInserts, numFindsPerInsert);
    sum += randomFindInternal<MAP>(3, upper32bit, numInserts, numFindsPerInsert);
    sum += randomFindInternal<MAP>(2, mediu32bit, numInserts, numFindsPerInsert);
//    randomFindInternal<MAP>(2, -1ull,      numInserts, numFindsPerInsert);
    sum += randomFindInternal<MAP>(1, upper32bit, numInserts, numFindsPerInsert);
    sum += randomFindInternal<MAP>(0, lower32bit, numInserts, numFindsPerInsert);

    if (sum != 123)
    printf(" nums = %zd total time = %.2f s\n", numInserts, now2sec() - ts);
}

static void runTest(int sflags, int eflags)
{
    const auto start = now2sec();

    if (sflags <= 1 && eflags >= 1)
    {
#if ABSL_HASH
        typedef absl::Hash<uint64_t> hash_func;
#elif STD_HASH
        typedef std::hash<uint64_t> hash_func;
#elif ANKERL_HASH
        using hash_func = ankerl::unordered_dense::hash<uint64_t>;
#else
        typedef robin_hood::hash<uint64_t> hash_func;
#endif

        puts("\nbench_IterateIntegers:");

#if QC_HASH
        { qc::hash::RawMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { fph::DynamicFphMap<uint64_t, uint64_t, fph::MixSeedHash<uint64_t>> emap; bench_IterateIntegers(emap); }
#endif

        { emhash5::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { emhash8::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { emhash7::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { emhash6::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
#if CXX17
        { ankerl::unordered_dense::map <uint64_t, uint64_t, hash_func> martin; bench_IterateIntegers(martin); }
#endif
#if CXX20
        { jg::dense_hash_map<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { rigtorp::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
#endif
#if ET
        { hrd_m::hash_map <uint64_t, uint64_t, hash_func> hmap; bench_IterateIntegers(hmap); }
        { tsl::robin_map     <uint64_t, uint64_t, hash_func> rmap; bench_IterateIntegers(rmap); }
        { robin_hood::unordered_map <uint64_t, uint64_t, hash_func> martin; bench_IterateIntegers(martin); }

#if X86_64
        { ska::flat_hash_map <uint64_t, uint64_t, hash_func> fmap; bench_IterateIntegers(fmap); }
#endif
        { phmap::flat_hash_map<uint64_t, uint64_t, hash_func> hmap; bench_IterateIntegers(hmap); }
#endif
#if X86
        { emilib::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { emilib3::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
        { emilib2::HashMap<uint64_t, uint64_t, hash_func> emap; bench_IterateIntegers(emap); }
#endif
#if HAVE_BOOST
        { boost::unordered_flat_map<uint64_t, uint64_t, hash_func> hmap; bench_IterateIntegers(hmap); }
#endif
#if CK_HMAP
        { ck::HashMap<uint64_t, uint64_t, hash_func> hmap; bench_IterateIntegers(hmap); }
#endif

#if ABSL_HMAP
        { absl::flat_hash_map<uint64_t, uint64_t, hash_func> hmap; bench_IterateIntegers(hmap); }
#endif
#if FOLLY_F14
        { folly::F14VectorMap<uint64_t, uint64_t, hash_func> hmap; bench_IterateIntegers(hmap); }
#endif
    }

    if (sflags <= 2 && eflags >= 2)
    {
#ifdef HOOD_HASH
        typedef robin_hood::hash<std::string> hash_func;
#elif ABSL_HASH
        typedef absl::Hash<std::string> hash_func;
//#elif AHASH_AHASH_H
//        typedef Ahash64er hash_func;
#elif ANKERL_HASH
        using hash_func = ankerl::unordered_dense::hash<std::string>;
#elif STD_HASH
        typedef std::hash<std::string> hash_func;
#else
        typedef WysHasher hash_func;
#endif
        puts("\nbench_randomFindString:");

#if CXX17
        { ankerl::unordered_dense::map<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
#endif

        { emhash8::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
        { emhash6::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
        { emhash5::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
#if QC_HASH
        { fph::DynamicFphMap<std::string, size_t, fph::MixSeedHash<std::string>> bench; bench_randomFindString(bench); }
#endif
        { emhash7::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
#if X86
        { emilib3::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
        { emilib2::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
        { emilib::HashMap<std::string,  size_t, hash_func> bench; bench_randomFindString(bench); }
#endif
#if HAVE_BOOST
        { boost::unordered_flat_map<std::string,  size_t, hash_func> bench; bench_randomFindString(bench); }
#endif
#if CK_HMAP //crash TODO
//        { ck::HashMap<std::string,  size_t, hash_func> bench; bench_randomFindString(bench); }
#endif

#if ET
        { hrd_m::hash_map <std::string, size_t, hash_func> hmap;   bench_randomFindString(hmap); }
        { tsl::robin_map  <std::string, size_t, hash_func> bench;     bench_randomFindString(bench); }
        { robin_hood::unordered_map <std::string, size_t, hash_func> bench; bench_randomFindString(bench); }

#if X86_64
        { ska::flat_hash_map<std::string, size_t, hash_func> bench;   bench_randomFindString(bench); }
#endif
        { phmap::flat_hash_map<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
#endif
#if CXX20
        { jg::dense_hash_map<std::string, int, hash_func> bench; bench_randomFindString(bench); }
        { rigtorp::HashMap<std::string, size_t, hash_func> bench; bench_randomFindString(bench); }
#endif
#if FOLLY_F14
        { folly::F14VectorMap<std::string, size_t, hash_func> bench;  bench_randomFindString(bench); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map<std::string, size_t, hash_func> bench;  bench_randomFindString(bench); }
#endif
    }

    if (sflags <= 3 && eflags >= 3)
    {
#ifdef HOOD_HASH
        typedef robin_hood::hash<std::string> hash_func;
#elif ABSL_HASH
        typedef absl::Hash<std::string> hash_func;
//#elif AHASH_AHASH_H
//        typedef Ahash64er hash_func;
#elif STD_HASH
        typedef std::hash<std::string> hash_func;
#elif ANKERL_HASH
        using hash_func = ankerl::unordered_dense::hash<std::string>;
#else
        typedef WysHasher hash_func;
#endif
        puts("\nbench_randomEraseString:");

#if X86
        { emilib2::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
        { emilib::HashMap<std::string,  int, hash_func> bench; bench_randomEraseString(bench); }
        { emilib3::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
        { emhash8::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
        { emhash7::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
        { emhash6::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
        { emhash5::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }

#if CXX17
        { ankerl::unordered_dense::map <std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
#if CXX20
        { rigtorp::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
        { jg::dense_hash_map<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif

#if QC_HASH
        { fph::DynamicFphMap<std::string, int, fph::MixSeedHash<std::string>> bench; bench_randomEraseString(bench); }
#endif

#if ET
        { hrd_m::hash_map <std::string, int, hash_func> hmap;   bench_randomEraseString(hmap); }
        { tsl::robin_map  <std::string, int, hash_func> bench; bench_randomEraseString(bench); }
        { robin_hood::unordered_map <std::string, int, hash_func> bench; bench_randomEraseString(bench); }

#if X86_64
        { ska::flat_hash_map<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
        { phmap::flat_hash_map<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
#if FOLLY_F14
        { folly::F14VectorMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
#if HAVE_BOOST
        { boost::unordered_flat_map<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
#if CK_HMAP //crash TODO
//        { ck::HashMap<std::string, int, hash_func> bench; bench_randomEraseString(bench); }
#endif
    }

    if (sflags <= 4 && eflags >= 4)
    {
#if ABSL_HASH
        typedef absl::Hash<size_t> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<size_t> hash_func;
#elif STD_HASH
        typedef std::hash<size_t> hash_func;
#elif ANKERL_HASH
        using hash_func = ankerl::unordered_dense::hash<size_t>;
#else
        typedef robin_hood::hash<size_t> hash_func;
#endif
        puts("\nbench_randomFind:");

        static constexpr size_t numInserts[]        = {123,     5234,  1234567, 12345678};
        static constexpr size_t numFindsPerInsert[] = {800000,  20000, 50, 5};
        for (size_t i = 0; i < sizeof(numInserts) / sizeof(numInserts[0]); i++)
        {
#if ET
            { tsl::robin_map <size_t, size_t, hash_func> rmap; bench_randomFind(rmap, numInserts[i], numFindsPerInsert[i]); }
            { robin_hood::unordered_map <size_t, size_t, hash_func> martin; bench_randomFind(martin, numInserts[i], numFindsPerInsert[i]); }

#if X86_64
            { ska::flat_hash_map <size_t, size_t, hash_func> fmap; bench_randomFind(fmap, numInserts[i], numFindsPerInsert[i]); }
#endif

            { phmap::flat_hash_map <size_t, size_t, hash_func> hmap; bench_randomFind(hmap, numInserts[i], numFindsPerInsert[i]); }
            { hrd_m::hash_map <size_t, size_t, hash_func> hmap; bench_randomFind(hmap, numInserts[i], numFindsPerInsert[i]); }
#endif
#if QC_HASH
            { fph::DynamicFphMap<size_t, size_t, fph::MixSeedHash<size_t>> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { qc::hash::RawMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
#endif
#if CXX20
            { jg::dense_hash_map<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { rigtorp::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
#endif
#if X86
            { emilib2::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { emilib3::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { emilib::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
#endif
#if ABSL_HMAP
            { absl::flat_hash_map <size_t, size_t, hash_func> hmap; bench_randomFind(hmap, numInserts[i], numFindsPerInsert[i]); }
#endif
#if HAVE_BOOST
            { boost::unordered_flat_map <size_t, size_t, hash_func> hmap; bench_randomFind(hmap, numInserts[i], numFindsPerInsert[i]); }
#endif
#if CK_HMAP
            { ck::HashMap <size_t, size_t, hash_func> hmap; bench_randomFind(hmap, numInserts[i], numFindsPerInsert[i]); }
#endif

#if CXX17
            { ankerl::unordered_dense::map<size_t, size_t, hash_func> martin; bench_randomFind(martin, numInserts[i], numFindsPerInsert[i]); }
#endif
            { emhash8::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { emhash5::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { emhash6::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
            { emhash7::HashMap<size_t, size_t, hash_func> emap; bench_randomFind(emap, numInserts[i], numFindsPerInsert[i]); }
#if FOLLY_F14
            { folly::F14VectorMap <size_t, size_t, hash_func> hmap; bench_randomFind(hmap, numInserts[i], numFindsPerInsert[i]); }
#endif
            putchar('\n');
        }
    }

    if (sflags <= 5 && eflags >= 5)
    {
#if ABSL_HASH
        typedef absl::Hash<int> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<int> hash_func;
#elif STD_HASH
        typedef std::hash<int> hash_func;
#elif ANKERL_HASH
        using hash_func = ankerl::unordered_dense::hash<int>;
#else
        typedef robin_hood::hash<int> hash_func;
#endif

        puts("\nbench_insert:");

#if ABSL_HMAP
        { absl::flat_hash_map <int, int, hash_func> amap; bench_insert(amap); }
#endif
#if HAVE_BOOST
        { boost::unordered_flat_map <int, int, hash_func> amap; bench_insert(amap); }
#endif
#if CK_HMAP
        { ck::HashMap <int, int, hash_func> amap; bench_insert(amap); }
#endif
#if FOLLY_F14
        { folly::F14VectorMap <int, int, hash_func> hmap; bench_insert(hmap); }
#endif
        { emhash7::HashMap<int, int, hash_func> emap; bench_insert(emap); }

#if CXX20
        { jg::dense_hash_map<int, int, hash_func> qmap; bench_insert(qmap); }
        { rigtorp::HashMap<int, int, hash_func> emap; bench_insert(emap); }
#endif
#if CXX17
        { ankerl::unordered_dense::map<int, int, hash_func> martin; bench_insert(martin); }
#endif

#if QC_HASH
        { qc::hash::RawMap<int, int, hash_func> qmap; bench_insert(qmap); }

#endif
        { emhash8::HashMap<int, int, hash_func> emap; bench_insert(emap); }
        { emhash6::HashMap<int, int, hash_func> emap; bench_insert(emap); }
        { emhash5::HashMap<int, int, hash_func> emap; bench_insert(emap); }
#if X86
        { emilib::HashMap<int, int, hash_func>  emap; bench_insert(emap); }
        { emilib2::HashMap<int, int, hash_func> emap; bench_insert(emap); }
        { emilib3::HashMap<int, int, hash_func> emap; bench_insert(emap); }
#endif
#if ET
        { hrd_m::hash_map <int, int, hash_func> hmap; bench_insert(hmap); }
        { tsl::robin_map  <int, int, hash_func> rmap; bench_insert(rmap); }
        { robin_hood::unordered_map <int, int, hash_func> martin; bench_insert(martin); }

#if X86_64
        { ska::flat_hash_map <int, int, hash_func> fmap; bench_insert(fmap); }
#endif
        { phmap::flat_hash_map <int, int, hash_func> hmap; bench_insert(hmap); }
#endif
    }

    if (sflags <= 6 && eflags >= 6)
    {
#if ABSL_HASH
        typedef absl::Hash<uint64_t> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<uint64_t> hash_func;
#elif STD_HASH
        typedef std::hash<uint64_t> hash_func;
#elif ANKERL_HASH
        using hash_func = ankerl::unordered_dense::hash<uint64_t>;
#else
        typedef robin_hood::hash<uint64_t> hash_func;
#endif

        puts("\nbench_randomInsertErase:");

#if HAVE_BOOST
        { boost::unordered_flat_map <uint64_t, uint64_t, hash_func> hmap; bench_randomInsertErase(hmap); }
#endif
#if CK_HMAP
        { ck::HashMap <uint64_t, uint64_t, hash_func> hmap; bench_randomInsertErase(hmap); }
#endif

#if ABSL_HMAP
        { absl::flat_hash_map <uint64_t, uint64_t, hash_func> hmap; bench_randomInsertErase(hmap); }
#endif
        { emhash8::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
        { emhash5::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
        { emhash7::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
        { emhash6::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
#if QC_HASH
        { fph::DynamicFphMap<uint64_t, uint64_t, fph::MixSeedHash<uint64_t>> emap; bench_randomInsertErase(emap); }
#if QC_HASH==2
        { qc::hash::RawMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); } //hang
#endif
#endif

#if X86
        { emilib3::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
        { emilib2::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
        { emilib::HashMap<uint64_t, uint64_t, hash_func>  emap; bench_randomInsertErase(emap); }
#endif
#if ET
        { hrd_m::hash_map <size_t, size_t, hash_func> hmap; bench_randomInsertErase(hmap); }
        { tsl::robin_map     <uint64_t, uint64_t, hash_func> rmap; bench_randomInsertErase(rmap); }
        { robin_hood::unordered_map <uint64_t, uint64_t, hash_func> martin; bench_randomInsertErase(martin); }

#if X86_64
        { ska::flat_hash_map <uint64_t, uint64_t, hash_func> fmap; bench_randomInsertErase(fmap); }
#endif
        { phmap::flat_hash_map <uint64_t, uint64_t, hash_func> hmap; bench_randomInsertErase(hmap); }
#endif
#if FOLLY_F14
        { folly::F14VectorMap <uint64_t, uint64_t, hash_func> hmap; bench_randomInsertErase(hmap); }
#endif
#if CXX17
        { ankerl::unordered_dense::map<uint64_t, uint64_t, hash_func> martin; bench_randomInsertErase(martin); }
#endif
#if CXX20
        { jg::dense_hash_map<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); }
        //{ rigtorp::HashMap<uint64_t, uint64_t, hash_func> emap; bench_randomInsertErase(emap); } //hange
#endif
    }

    if (sflags <= 7 && eflags >= 7)
    {
#if ABSL_HASH
        typedef absl::Hash<int> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<int> hash_func;
#elif STD_HASH
        typedef std::hash<int> hash_func;
#elif HOOD_HASH
        typedef robin_hood::hash<int> hash_func;
#else
        typedef robin_hood::hash<int> hash_func;
#endif

        puts("\nbench_randomDistinct2:");
#if QC_HASH
        { qc::hash::RawMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
//        { fph::DynamicFphMap<int, int, fph::MixSeedHash<int>> emap; bench_randomDistinct2(emap); } //hang
#endif

#if CXX20
        { jg::dense_hash_map<int, int, hash_func> emap; bench_randomDistinct2(emap); }
        { rigtorp::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
#endif

#if CXX17
        { ankerl::unordered_dense::map <int, int, hash_func> martin; bench_randomDistinct2(martin); }
#endif

        { emhash8::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
        { emhash6::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
        { emhash5::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
        { emhash7::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }

#if X86
        { emilib::HashMap<int, int, hash_func> emap;  bench_randomDistinct2(emap); }
        { emilib2::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
        { emilib3::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
#endif
#if ET
        { hrd_m::hash_map  <int, int, hash_func> hmap; bench_randomDistinct2(hmap); }
        { tsl::robin_map   <int, int, hash_func> rmap; bench_randomDistinct2(rmap); }
        { robin_hood::unordered_map <int, int, hash_func> martin; bench_randomDistinct2(martin); }

#if X86_64
        { ska::flat_hash_map <int, int, hash_func> fmap; bench_randomDistinct2(fmap); }
#endif
        { phmap::flat_hash_map <int, int, hash_func> hmap; bench_randomDistinct2(hmap); }
#endif
#if HAVE_BOOST
        { boost::unordered_flat_map <int, int, hash_func> hmap; bench_randomDistinct2(hmap); }
#endif
#if CK_HMAP
        { ck::HashMap<int, int, hash_func> emap; bench_randomDistinct2(emap); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map <int, int, hash_func> hmap; bench_randomDistinct2(hmap); }
#endif

#if FOLLY_F14
        { folly::F14VectorMap <int, int, hash_func> hmap; bench_randomDistinct2(hmap); }
#endif
    }

    if (sflags <= 8 && eflags >= 8)
    {
#if STD_HASH
        typedef std::hash<uint64_t> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<uint64_t> hash_func;
#else
        typedef robin_hood::hash<uint64_t> hash_func;
#endif
        puts("\nbench_copy:");

        { emhash6::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
        { emhash5::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
        { emhash7::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
        { emhash8::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }

#if QC_HASH
        { qc::hash::RawMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
#endif

#if CXX20
        { jg::dense_hash_map<uint64_t, int, hash_func> emap; bench_copy(emap); }
        { rigtorp::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
#endif
#if CXX17
        { ankerl::unordered_dense::map <uint64_t, int, hash_func> martin; bench_copy(martin); }
#endif


#if X86
        { emilib::HashMap<uint64_t, int, hash_func> emap;  bench_copy(emap); }
        { emilib2::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
        { emilib3::HashMap<uint64_t, int, hash_func> emap; bench_copy(emap); }
#endif
#if ET
        { tsl::robin_map     <uint64_t, int, hash_func> rmap; bench_copy(rmap); }
        { robin_hood::unordered_map <uint64_t, int, hash_func> martin; bench_copy(martin); }

#if X86_64
        { ska::flat_hash_map <uint64_t, int, hash_func> fmap; bench_copy(fmap); }
#endif
        { phmap::flat_hash_map <uint64_t, int, hash_func> hmap; bench_copy(hmap); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map <uint64_t, int, hash_func> hmap; bench_copy(hmap); }
#endif
#if HAVE_BOOST
        { boost::unordered_flat_map <uint64_t, int, hash_func> hmap; bench_copy(hmap); }
#endif
#if CK_HMAP
//        { ck::HashMap <uint64_t, int, hash_func> hmap; bench_copy(hmap); }
#endif

#if FOLLY_F14
        { folly::F14VectorMap <uint64_t, int, hash_func> hmap; bench_copy(hmap); }
#endif
    }

    if (sflags <= 9 && eflags >= 9)
    {
#if ABSL_HASH
        typedef absl::Hash<uint64_t> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<uint64_t> hash_func;
#elif STD_HASH
        typedef std::hash<uint64_t> hash_func;
#elif HOOD_HASH
        typedef robin_hood::hash<uint64_t> hash_func;
#else
        typedef ankerl::unordered_dense::hash<uint64_t> hash_func;
#endif
        puts("\nbench_knucleotide:");

#if QC_HASH
        { qc::hash::RawMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
#endif

#if CXX20
        { jg::dense_hash_map<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
        { rigtorp::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
#endif
        { ankerl::unordered_dense::map <uint64_t, uint32_t, hash_func> martin; bench_knucleotide(martin); }
#if HAVE_BOOST
        { boost::unordered_flat_map <uint64_t, uint32_t, hash_func> hmap; bench_knucleotide(hmap); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map <uint64_t, uint32_t, hash_func> pmap; bench_knucleotide(pmap); }
#endif

        { emhash6::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
        { emhash5::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
        { emhash7::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
        { emhash8::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }

#if X86
        { emilib::HashMap <uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
        { emilib2::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
        { emilib3::HashMap<uint64_t, uint32_t, hash_func> emap; bench_knucleotide(emap); }
#endif
#if ET
        { hrd_m::hash_map <uint64_t, uint32_t, hash_func> hmap; bench_knucleotide(hmap); }
        { tsl::robin_map  <uint64_t, uint32_t, hash_func> rmap; bench_knucleotide(rmap); }
        { robin_hood::unordered_map <uint64_t, uint32_t, hash_func> martin; bench_knucleotide(martin); }

#if X86_64
        { ska::flat_hash_map <uint64_t, uint32_t, hash_func> fmap; bench_knucleotide(fmap); }
#endif
        { phmap::flat_hash_map <uint64_t, uint32_t, hash_func> pmap; bench_knucleotide(pmap); }
#endif

#if FOLLY
        { folly::F14VectorMap <uint64_t, uint32_t, hash_func> pmap; bench_knucleotide(pmap); }
#endif
#if CK_HMAP
        { ck::HashMap <uint64_t, uint32_t, hash_func> hmap; bench_knucleotide(hmap); }
#endif
    }

    if (sflags <= 10 && eflags >= 10)
    {
#if ABSL_HASH
        typedef absl::Hash<uint32_t> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<uint32_t> hash_func;
#elif STD_HASH
        typedef std::hash<uint32_t> hash_func;
#elif HOOD_HASH
        typedef robin_hood::hash<uint32_t> hash_func;
#else
        typedef ankerl::unordered_dense::hash<uint32_t> hash_func;
#endif
        puts("\nbench_GameOfLife:\n");

        { emhash6::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
        { emhash5::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
        { emhash7::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
        { emhash8::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }

#if QC_HASH
        { qc::hash::RawMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
#endif

#if CXX20
        { jg::dense_hash_map<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
        { rigtorp::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
#endif
        { ankerl::unordered_dense::map <uint32_t, bool, hash_func> martin; bench_GameOfLife(martin); }
#if HAVE_BOOST
        { boost::unordered_flat_map <uint32_t, bool, hash_func> hmap; bench_GameOfLife(hmap); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map <uint32_t, bool, hash_func> pmap; bench_GameOfLife(pmap); }
#endif

#if X86
        { emilib::HashMap <uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
        { emilib2::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
        { emilib3::HashMap<uint32_t, bool, hash_func> emap; bench_GameOfLife(emap); }
#endif
#if ET
        { hrd_m::hash_map <uint32_t, bool, hash_func> hmap; bench_GameOfLife(hmap); }
        { tsl::robin_map  <uint32_t, bool, hash_func> rmap; bench_GameOfLife(rmap); }
        { robin_hood::unordered_map <uint32_t, bool, hash_func> martin; bench_GameOfLife(martin); }

#if X86_64
        { ska::flat_hash_map <uint32_t, bool, hash_func> fmap; bench_GameOfLife(fmap); }
#endif
        { phmap::flat_hash_map <uint32_t, bool, hash_func> pmap; bench_GameOfLife(pmap); }
#endif

#if FOLLY
        { folly::F14VectorMap <uint32_t, bool, hash_func> pmap; bench_GameOfLife(pmap); }
#endif
#if CK_HMAP
        //{ ck::HashMap <uint32_t, bool, hash_func> hmap; bench_GameOfLife(hmap); }
#endif
    }

    if (sflags <= 11 && eflags >= 11)
    {
#if ABSL_HASH
        typedef absl::Hash<uint32_t> hash_func;
#elif FIB_HASH
        typedef Int64Hasher<int> hash_func;
#elif STD_HASH
        typedef std::hash<int> hash_func;
#elif HOOD_HASH
        typedef robin_hood::hash<int> hash_func;
#else
        typedef ankerl::unordered_dense::hash<int> hash_func;
#endif

        puts("\nbench_AccidentallyQuadratic (10M int insert.copy.iterator):");

        { emhash6::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
        { emhash5::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
        { emhash7::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
        { emhash8::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }

#if QC_HASH
        { qc::hash::RawMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
#endif

#if CXX20
        { jg::dense_hash_map<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
        { rigtorp::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
#endif
        { ankerl::unordered_dense::map <int, int, hash_func> martin; bench_AccidentallyQuadratic(martin); }
#if HAVE_BOOST
        { boost::unordered_flat_map <int, int, hash_func> hmap; bench_AccidentallyQuadratic(hmap); }
#endif
#if ABSL_HMAP
        { absl::flat_hash_map <int, int, hash_func> pmap; bench_AccidentallyQuadratic(pmap); }
#endif

#if X86
        { emilib::HashMap <int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
        { emilib2::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
        { emilib3::HashMap<int, int, hash_func> emap; bench_AccidentallyQuadratic(emap); }
#endif
#if ET
        { hrd_m::hash_map <int, int, hash_func> hmap; bench_AccidentallyQuadratic(hmap); }
        { tsl::robin_map  <int, int, hash_func> rmap; bench_AccidentallyQuadratic(rmap); }
        { robin_hood::unordered_map <int, int, hash_func> martin; bench_AccidentallyQuadratic(martin); }

#if X86_64
        { ska::flat_hash_map <int, int, hash_func> fmap; bench_AccidentallyQuadratic(fmap); }
#endif
        { phmap::flat_hash_map <int, int, hash_func> pmap; bench_AccidentallyQuadratic(pmap); }
#endif

#if FOLLY
        { folly::F14VectorMap <int, int, hash_func> pmap; bench_AccidentallyQuadratic(pmap); }
#endif
#if CK_HMAP
        //{ ck::HashMap <int, int, hash_func> hmap; bench_AccidentallyQuadratic(hmap); }
#endif

    }

    printf("\ntotal time = %.2f s", now2sec() - start);
}

static void checkSet(const std::string& map_name)
{
    if (show_name.count(map_name) == 1)
        show_name.erase(map_name);
    else
        show_name.emplace(map_name, map_name);
}

int main(int argc, char* argv[])
{
    printInfo(nullptr);
    puts("./test [2-9mptseb0d2 rjqf] n");
    for (auto& m : show_name)
        printf("%10s %20s\n", m.first.c_str(), m.second.c_str());

    int sflags = 1, eflags = 11;
    if (argc > 1) {
        printf("cmd agrs = %s\n", argv[1]);
        for (int c = argv[1][0], i = 0; c != '\0'; c = argv[1][++i]) {
            if (c > '4' && c <= '8') {
                std::string map_name("emhash");
                map_name += c;
                checkSet(map_name);
            } else if (c == 'm') {
                checkSet("robin_hood");
                checkSet("ankerl");
            }
            else if (c == 'p')
                checkSet("phmap");
            else if (c == 'a')
                checkSet("absl");
            else if (c == 't')
                checkSet("robin_map");
            else if (c == 's')
                checkSet("ska");
            else if (c == 'h')
                checkSet("hrd_m");
            else if (c == '1')
                checkSet("emilib");
            else if (c == '2')
                checkSet("emilib2");
            else if (c == '3')
                checkSet("emilib3");
            else if (c == 'j')
                checkSet("jg");
            else if (c == 'r')
                checkSet("rigtorp");
            else if (c == 'k') {
                checkSet("HashMapTable");
                checkSet("HashMapCell");
            }
            else if (c == 'q')
                checkSet("qc");
            else if (c == 'f')
                checkSet("fph");
            else if (c == 'b') {
                 if (isdigit(argv[1][i + 1]))
                    sflags = atoi(&argv[1][++i]);
                else
                    checkSet("boost");
                if (isdigit(argv[1][i + 1])) i++;
            }
            else if (c == 'e') {
                eflags = atoi(&argv[1][++i]);
                if (isdigit(argv[1][i + 1])) i++;
            }
            else if (c == 'l')
              max_lf = atoi(&argv[1][++i]) / 10.0f;
        }
    }

    printf("test hash: max_load_factor = %.2f\n", max_lf);
    for (auto& m : show_name)
        printf("%10s %20s\n", m.first.c_str(), m.second.c_str());

    runTest(sflags, eflags);
    return 0;
}

