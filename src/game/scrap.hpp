#pragma once

// The Scrap window's actions on vehicles (docs/spec/03 §15, confirmed:
// binary): Scrap, Analyze, Mothball, Unmothball, Retrofit, Self-Destruct and
// Fire On. The window gives the action to every selected vehicle, one after
// another, as one command each (cmd::Scrap, cmd::Analyze, cmd::Mothball,
// cmd::Retrofit, cmd::SelfDestruct, cmd::FireOn):
// - in a turn-based game the action is carried out at once, its test made
//   again just before, and the order list is not touched;
// - in a simultaneous game nothing happens yet: the vehicle's order list is
//   cleared (Repeat goes off) and the action becomes its only order. Movement
//   carries it out at the vehicle's first action of the turn, testing again;
//   a failed test fails the order and clears the list (movement.cpp).

#include "game/rules.hpp"
#include "game/state.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opense4::game {

struct TurnContext;

enum class ScrapAction : uint8_t { Scrap, Analyze, Mothball, Unmothball, Retrofit, SelfDestruct, FireOn };

// The order each action becomes in a simultaneous game, and back.
OrderKind scrapOrderKind(ScrapAction a);
std::optional<ScrapAction> scrapActionOf(OrderKind k);
// The actions' own order kinds (Self-Destruct excepted, an order of its own
// before the window used it): they come only from the window's commands.
bool scrapWindowOrder(OrderKind k);

// The window lists only own vehicles in the sector that are in no fleet and
// not cloaked (spec 03 §15, §19 Q74).
bool scrapListed(const Vehicle& v, EmpireId e);
// An own working space yard in the sector: an own ship or base with a
// working `Space Yard` that is not cloaked (the vehicle itself included), or
// an own colony with a space yard that is not cloaked (spec 03 §15, spec 01
// §6.9).
bool scrapYardAt(const Rules& r, const GameState& s, EmpireId e, Location where);

// Why `a` cannot be carried out on `v` for `e` now (empty: it can): `v` must
// be listed, then the action's own test (spec 03 §15):
// - Scrap: a yard, and not a drone group or minefield;
// - Analyze: a ship or base, and a yard;
// - Mothball: a ship or base, a yard, status Normal and no cargo;
// - Unmothball: status Mothballed and every resource of its cost in stock;
// - Retrofit: retrofitProblem;
// - Self-Destruct: movement::canSelfDestruct;
// - Fire On: canBeFiredOn.
std::string scrapActionProblem(const Rules& r, const GameState& s, EmpireId e, const Vehicle& v, ScrapAction a, DesignId retrofitTo = {});

// Carries the action out on `v`, whose test passed. A vehicle destroyed is
// left with no units (count 0); the caller removes dead vehicles and
// recalculates sight.
// - Scrap: the refund (scrapRefund), "Number Scrapped" (a ship or base 1, a
//   unit group each living unit, inferred), a Construction entry; cargo lost.
// - Analyze: no refund; "Number Scrapped" + 1 on its design; one level per
//   pair of analyzePairs (research::analyzeLevel).
// - Mothball: status Mothballed, its orders, queue, supply and movement gone.
// - Unmothball: the cost paid, status Normal, supply full when unlimited,
//   refilled at a resupply depot, else 0.
// - Retrofit: retrofitVehicle.
// - Self-Destruct: counts as scrapped, not lost (spec 04 §15): a ship or
//   base 1, a unit group each unit (inferred); a Misc entry.
// - Fire On: "Number Lost" (a ship or base 1, a fighter or satellite group or
//   minefield each living unit per design, a drone group nothing) and one
//   Construction entry with Goto to the sector.
void carryOutScrapAction(TurnContext& ctx, Vehicle& v, ScrapAction a, DesignId retrofitTo = {});

// ---- Analyze (spec 03 §15, confirmed: binary) -------------------------------------------------------

struct TechPair {
    ruleset::TechAreaId area;
    int level = 0;
    bool operator==(const TechPair&) const = default;
};
// What `v` can teach empire `e`, worked out once: the tech requirements of
// its design's components in design order, destroyed ones skipped, then those
// of its hull; a pair (area, level) is kept when `e`'s level in the area is
// below it and the same pair is not in the list yet.
std::vector<TechPair> analyzePairs(const Rules& r, const GameState& s, EmpireId e, const Vehicle& v);
// The Scrap window's Research Potential word for a number of pairs: None,
// Minor, Moderate, Sizable, Major (4 or more).
std::string_view researchPotentialWord(size_t pairs);

// ---- Fire On (spec 03 §15, confirmed: binary) -------------------------------------------------------

// Armed for Fire On: a ship or base that is not mothballed and whose design
// has a Direct Fire or Seeking component (destroyed ones count); a fighter
// group whose fighters' designs have any weapon (Point-Defense and Warhead
// included); never a satellite group, minefield or drone group.
bool armedForFireOn(const Rules& r, const GameState& s, const Vehicle& v);
// Another vehicle of the same owner in the sector is armed (it may be
// cloaked, in a fleet, out of supply or movement). Nothing else is tested.
bool canBeFiredOn(const Rules& r, const GameState& s, const Vehicle& v);

// ---- Retrofit (spec 03 §14, confirmed: binary) ------------------------------------------------------

// The checks of a retrofit in order, the first failure giving its reason
// (empty: it can): identical designs, a yard, the hull, cargo, the cost in
// stock, space yards and colony modules not added, the cost limit. `cost`
// receives what it would cost (taken only when a component is added).
std::string retrofitProblem(const Rules& r, const GameState& s, EmpireId e, const Vehicle& v, DesignId to, Resources* cost = nullptr);
// Paired components keep their state, unpaired target ones start destroyed;
// movement and supply are clamped to the new maxima; the cost is taken when
// a component was added.
void retrofitVehicle(TurnContext& ctx, Vehicle& v, DesignId to);

} // namespace opense4::game
