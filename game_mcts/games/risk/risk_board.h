#ifndef RISK_GAME_BOARD_H
#define RISK_GAME_BOARD_H

#include <array>
#include <cstdint>
#include <string_view>

namespace risk_game {

enum class Country : uint8_t {
  Afghanistan,
  Alaska,
  Alberta,
  Argentina,
  Brazil,
  Central_Africa,
  Central_America,
  China,
  East_Africa,
  Eastern_Australia,
  Eastern_Canada,
  Eastern_United_States,
  Egypt,
  Great_Britain,
  Greenland,
  Iceland,
  India,
  Indonesia,
  Irkutsk,
  Japan,
  Kamchatka,
  Madagascar,
  Middle_East,
  Mongolia,
  New_Guinea,
  North_Africa,
  Northern_Europe,
  Northwest_Territory,
  Ontario,
  Peru,
  Scandinavia,
  Siberia,
  South_Africa,
  Southeast_Asia,
  Southern_Europe,
  Ukraine,
  Ural,
  Venezuela,
  Western_Australia,
  Western_Europe,
  Western_United_States,
  Yakutsk,
  COUNT
};

struct CountryData {
  Country id;
  std::string_view name;
  std::size_t neighbor_count;
  std::array<Country, 6> neighbors;  // Max degree in Risk map is 6
};

static constexpr std::array<CountryData,
                            static_cast<std::size_t>(Country::COUNT)>
    Board = {
        {{Country::Afghanistan,
          "Afghanistan",
          5,
          {Country::China, Country::India, Country::Middle_East,
           Country::Ukraine, Country::Ural}},
         {Country::Alaska,
          "Alaska",
          3,
          {Country::Alberta, Country::Kamchatka, Country::Northwest_Territory}},
         {Country::Alberta,
          "Alberta",
          4,
          {Country::Alaska, Country::Northwest_Territory, Country::Ontario,
           Country::Western_United_States}},
         {Country::Argentina, "Argentina", 2, {Country::Brazil, Country::Peru}},
         {Country::Brazil,
          "Brazil",
          4,
          {Country::Argentina, Country::North_Africa, Country::Peru,
           Country::Venezuela}},
         {Country::Central_Africa,
          "Central Africa",
          3,
          {Country::East_Africa, Country::North_Africa, Country::South_Africa}},
         {Country::Central_America,
          "Central America",
          3,
          {Country::Eastern_United_States, Country::Venezuela,
           Country::Western_United_States}},
         {Country::China,
          "China",
          6,
          {Country::Afghanistan, Country::India, Country::Mongolia,
           Country::Siberia, Country::Southeast_Asia, Country::Ural}},
         {Country::East_Africa,
          "East Africa",
          6,
          {Country::Central_Africa, Country::Egypt, Country::Madagascar,
           Country::Middle_East, Country::North_Africa, Country::South_Africa}},
         {Country::Eastern_Australia,
          "Eastern Australia",
          2,
          {Country::New_Guinea, Country::Western_Australia}},
         {Country::Eastern_Canada,
          "Eastern Canada",
          3,
          {Country::Eastern_United_States, Country::Greenland,
           Country::Ontario}},
         {Country::Eastern_United_States,
          "Eastern United States",
          4,
          {Country::Central_America, Country::Eastern_Canada, Country::Ontario,
           Country::Western_United_States}},
         {Country::Egypt,
          "Egypt",
          4,
          {Country::East_Africa, Country::Middle_East, Country::North_Africa,
           Country::Southern_Europe}},
         {Country::Great_Britain,
          "Great Britain",
          4,
          {Country::Iceland, Country::Northern_Europe, Country::Scandinavia,
           Country::Western_Europe}},
         {Country::Greenland,
          "Greenland",
          4,
          {Country::Eastern_Canada, Country::Iceland,
           Country::Northwest_Territory, Country::Ontario}},
         {Country::Iceland,
          "Iceland",
          3,
          {Country::Great_Britain, Country::Greenland, Country::Scandinavia}},
         {Country::India,
          "India",
          4,
          {Country::Afghanistan, Country::China, Country::Middle_East,
           Country::Southeast_Asia}},
         {Country::Indonesia,
          "Indonesia",
          3,
          {Country::New_Guinea, Country::Southeast_Asia,
           Country::Western_Australia}},
         {Country::Irkutsk,
          "Irkutsk",
          4,
          {Country::Kamchatka, Country::Mongolia, Country::Siberia,
           Country::Yakutsk}},
         {Country::Japan, "Japan", 2, {Country::Kamchatka, Country::Mongolia}},
         {Country::Kamchatka,
          "Kamchatka",
          5,
          {Country::Alaska, Country::Irkutsk, Country::Japan, Country::Mongolia,
           Country::Yakutsk}},
         {Country::Madagascar,
          "Madagascar",
          2,
          {Country::East_Africa, Country::South_Africa}},
         {Country::Middle_East,
          "Middle East",
          6,
          {Country::Afghanistan, Country::East_Africa, Country::Egypt,
           Country::India, Country::Southern_Europe, Country::Ukraine}},
         {Country::Mongolia,
          "Mongolia",
          5,
          {Country::China, Country::Irkutsk, Country::Japan, Country::Kamchatka,
           Country::Siberia}},
         {Country::New_Guinea,
          "New Guinea",
          2,
          {Country::Eastern_Australia, Country::Indonesia}},
         {Country::North_Africa,
          "North Africa",
          6,
          {Country::Brazil, Country::Central_Africa, Country::East_Africa,
           Country::Egypt, Country::Southern_Europe, Country::Western_Europe}},
         {Country::Northern_Europe,
          "Northern Europe",
          5,
          {Country::Great_Britain, Country::Scandinavia,
           Country::Southern_Europe, Country::Ukraine,
           Country::Western_Europe}},
         {Country::Northwest_Territory,
          "Northwest Territory",
          4,
          {Country::Alaska, Country::Alberta, Country::Greenland,
           Country::Ontario}},
         {Country::Ontario,
          "Ontario",
          6,
          {Country::Alberta, Country::Eastern_Canada,
           Country::Eastern_United_States, Country::Greenland,
           Country::Northwest_Territory, Country::Western_United_States}},
         {Country::Peru,
          "Peru",
          3,
          {Country::Argentina, Country::Brazil, Country::Venezuela}},
         {Country::Scandinavia,
          "Scandinavia",
          4,
          {Country::Great_Britain, Country::Iceland, Country::Northern_Europe,
           Country::Ukraine}},
         {Country::Siberia,
          "Siberia",
          5,
          {Country::China, Country::Irkutsk, Country::Mongolia, Country::Ural,
           Country::Yakutsk}},
         {Country::South_Africa,
          "South Africa",
          3,
          {Country::Central_Africa, Country::East_Africa, Country::Madagascar}},
         {Country::Southeast_Asia,
          "Southeast Asia",
          3,
          {Country::China, Country::India, Country::Indonesia}},
         {Country::Southern_Europe,
          "Southern Europe",
          6,
          {Country::Egypt, Country::Middle_East, Country::North_Africa,
           Country::Northern_Europe, Country::Ukraine,
           Country::Western_Europe}},
         {Country::Ukraine,
          "Ukraine",
          6,
          {Country::Afghanistan, Country::Middle_East, Country::Northern_Europe,
           Country::Scandinavia, Country::Southern_Europe, Country::Ural}},
         {Country::Ural,
          "Ural",
          4,
          {Country::Afghanistan, Country::China, Country::Siberia,
           Country::Ukraine}},
         {Country::Venezuela,
          "Venezuela",
          3,
          {Country::Brazil, Country::Central_America, Country::Peru}},
         {Country::Western_Australia,
          "Western Australia",
          2,
          {Country::Eastern_Australia, Country::Indonesia}},
         {Country::Western_Europe,
          "Western Europe",
          4,
          {Country::Great_Britain, Country::North_Africa,
           Country::Northern_Europe, Country::Southern_Europe}},
         {Country::Western_United_States,
          "Western United States",
          4,
          {Country::Alberta, Country::Central_America,
           Country::Eastern_United_States, Country::Ontario}},
         {Country::Yakutsk,
          "Yakutsk",
          3,
          {Country::Irkutsk, Country::Kamchatka, Country::Siberia}}}};

enum class Continent {
  Africa,
  Asia,
  Australia,
  Europe,
  North_America,
  South_America,
  Count
};

struct ContinentData {
  Continent id;
  std::string_view name;

  uint8_t bonus_reinforcements;

  std::size_t country_count;
  std::array<Country, 12> countries;
};

static constexpr std::array<ContinentData,
                            static_cast<std::size_t>(Continent::Count)>
    Continents = {
        {{Continent::Africa,
          "Africa",
          3,
          6,
          {Country::Central_Africa, Country::East_Africa, Country::Egypt,
           Country::Madagascar, Country::North_Africa, Country::South_Africa}},
         {Continent::Asia,
          "Asia",
          7,
          12,
          {Country::Afghanistan, Country::China, Country::India,
           Country::Irkutsk, Country::Japan, Country::Kamchatka,
           Country::Middle_East, Country::Mongolia, Country::Siberia,
           Country::Southeast_Asia, Country::Ural, Country::Yakutsk}},
         {Continent::Australia,
          "Australia",
          2,
          4,
          {Country::Eastern_Australia, Country::Indonesia, Country::New_Guinea,
           Country::Western_Australia}},
         {Continent::Europe,
          "Europe",
          5,
          7,
          {Country::Great_Britain, Country::Iceland, Country::Northern_Europe,
           Country::Scandinavia, Country::Southern_Europe, Country::Ukraine,
           Country::Western_Europe}},
         {Continent::North_America,
          "North America",
          5,
          9,
          {Country::Alaska, Country::Alberta, Country::Central_America,
           Country::Eastern_Canada, Country::Eastern_United_States,
           Country::Greenland, Country::Northwest_Territory, Country::Ontario,
           Country::Western_United_States}},
         {Continent::South_America,
          "South America",
          2,
          4,
          {Country::Argentina, Country::Brazil, Country::Peru,
           Country::Venezuela}}}};

constexpr void VisitAllNeighborEdges(auto &&visitor) {
#pragma GCC unroll 42
  for (const auto &country_data : Board) {
    for (std::size_t i = 0; i < country_data.neighbor_count; ++i) {
      Country neighbor = country_data.neighbors[i];
      visitor(country_data.id, neighbor);
    }
  }
}

namespace internal {

constexpr size_t _CountEdges() {
  size_t edge_count = 0;
  VisitAllNeighborEdges([&](Country, Country) { ++edge_count; });
  return edge_count;
}
static_assert(internal::_CountEdges() == 164);

constexpr std::array<std::pair<Country, Country>, _CountEdges()>
_GetAllNeighborEdges() {
  std::array<std::pair<Country, Country>, _CountEdges()> edges{};
  size_t index = 0;
  VisitAllNeighborEdges(
      [&](Country src, Country tgt) { edges[index++] = {src, tgt}; });
  return edges;
}
}  // namespace internal
static constexpr auto kAllNeighborEdges = internal::_GetAllNeighborEdges();
static_assert(sizeof(kAllNeighborEdges) == 328);

namespace internal {
constexpr std::array<uint64_t, static_cast<size_t>(Country::COUNT)>
_GetNeighborMasks() {
  std::array<uint64_t, static_cast<size_t>(Country::COUNT)> masks{};
  VisitAllNeighborEdges([&](Country src, Country tgt) {
    masks[static_cast<size_t>(src)] |= uint64_t{1} << static_cast<size_t>(tgt);
  });
  return masks;
}
}  // namespace internal

// Bit i of kNeighborMasks[c] is set iff territory i borders territory c.
static constexpr auto kNeighborMasks = internal::_GetNeighborMasks();

namespace internal {
constexpr std::array<uint64_t, static_cast<size_t>(Continent::Count)>
_GetContinentMasks() {
  std::array<uint64_t, static_cast<size_t>(Continent::Count)> masks{};
  for (size_t c = 0; c < masks.size(); ++c) {
    for (size_t i = 0; i < Continents[c].country_count; ++i) {
      masks[c] |= uint64_t{1}
                  << static_cast<size_t>(Continents[c].countries[i]);
    }
  }
  return masks;
}
}  // namespace internal

// Bit i of kContinentMasks[c] is set iff territory i belongs to continent c.
static constexpr auto kContinentMasks = internal::_GetContinentMasks();

}  // namespace risk_game

#endif  // RISK_GAME_BOARD_H