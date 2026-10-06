"""Hegemon's diplomacy: peace with everyone but the rival it means to beat.

- Every rival in contact that is not the target is offered non-aggression (at most
  every few turns): at non-aggression or better the classic AI never targets us, and our
  ships in its space do not keep it on the defensive.
- Offers of non-aggression or better from anyone but the target are accepted; demands
  for tribute, systems or surrender are refused; gifts are taken.
- When the strategy goes to war, a treaty with the target is broken first (battles
  only happen below non-aggression).
The classic AI accepts a treaty when its anger toward us is low enough, so offers are
made early, before our expansion angers it, and repeated."""

PEACE = ("non_aggression", "trade_alliance", "trade_research_alliance", "military_alliance", "partnership")
GOOD_TREATIES = ("non_aggression",)
OFFER_EVERY = 6


class Diplomacy:
    def __init__(self, world, mem, strategy, tune=None):
        self.w = world
        self.mem = mem
        self.strategy = strategy
        self.tune = tune or {}
        d = mem.get("diplo")
        if not isinstance(d, dict):
            d = {"offered": {}}
            mem["diplo"] = d
        self.d = d
        self.commands = []

    def plan(self):
        w = self.w
        target = self.strategy.target if self.strategy is not None else None
        war = self.strategy is not None and self.strategy.phase == "war"
        # Answer what was sent to us.
        for m in w.d["messages"]:
            if m["to_empire"] != w.me or m["answered"] or not m["delivered"] or m["from_empire"] == w.me:
                continue
            frm = m["from_empire"]
            t = m["type"]
            accept = None
            if t in ("propose_treaty", "counter_treaty"):
                accept = m["treaty"] in GOOD_TREATIES and frm != target and m["treaty"] not in ("subjugation", "protectorate")
            elif t in ("gift", "tribute"):
                accept = True
            elif t.startswith("demand_") or t.startswith("request_") or t in ("propose_trade", "counter_trade"):
                accept = False
            if accept is not None:
                self.commands.append({"kind": "answer_message", "message": m["id"], "accept": accept,
                                      "text": "Agreed." if accept else "No."})
        # Offer peace to everyone but the target; break with the target for war.
        offered = self.d["offered"]
        for e in w.living_rivals():
            eid = e["id"]
            rel = e["relation"]
            if rel is None or not rel["contact"] or e["neutral"]:
                continue
            treaty = rel["treaty"]
            key = str(eid)
            if eid == target:
                if war and treaty in PEACE:
                    self.commands.append({"kind": "send_message", "message": {"to_empire": eid, "type": "break_treaty",
                                                                             "treaty": treaty, "text": "Our agreement ends."}})
                continue
            if treaty in PEACE:
                continue
            if rel["message_sent_this_turn"]:
                continue
            last = offered.get(key, -99)
            if w.turn - last < OFFER_EVERY:
                continue
            offered[key] = w.turn
            self.commands.append({"kind": "send_message", "message": {"to_empire": eid, "type": "propose_treaty",
                                                                     "treaty": "non_aggression", "text": "Let us keep the peace."}})
        return self.commands
