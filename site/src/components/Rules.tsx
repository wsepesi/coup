import { ROLE_COLORS } from "@/lib/constants";

function RoleTag({ name, symbol }: { name: string; symbol: string }) {
  return <span style={{ color: ROLE_COLORS[name] }}>{symbol} {name}</span>;
}

export default function RulesContent() {
  return (
    <div className="font-mono text-sm">
      {/* Two-column grid on md+, single column on small screens */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-x-8 gap-y-5">
        {/* Left column */}
        <div className="space-y-5">
          <div>
            <h2 className="text-text-bright text-base">OVERVIEW</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <p className="text-text-default">
              Coup is a game of bluffing and deduction for 2-6 players. Each player starts with 2 influence
              cards (face down) and 2 coins (in a 2-player game, the starting player gets just 1). Last player with influence wins.
            </p>
            <p className="text-text-default mt-2">
              You can claim ANY role for your action, even if you don{"'"}t have it — but if someone challenges
              you and you{"'"}re bluffing, you lose a card.
            </p>
          </div>

          <div>
            <h2 className="text-text-bright text-base">THE 5 ROLES</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <div className="space-y-3 ml-2">
              <div>
                <RoleTag name="Duke" symbol="♦" />
                <div className="text-text-dim ml-4">Action: Tax — take 3 coins from the treasury</div>
                <div className="text-text-dim ml-4">Blocks: Foreign Aid</div>
              </div>
              <div>
                <RoleTag name="Assassin" symbol="†" />
                <div className="text-text-dim ml-4">Action: Assassinate — pay 3 coins, target loses influence</div>
                <div className="text-text-dim ml-4">Blocked by: Contessa</div>
              </div>
              <div>
                <RoleTag name="Captain" symbol="⚓" />
                <div className="text-text-dim ml-4">Action: Steal — take 2 coins from another player</div>
                <div className="text-text-dim ml-4">Blocks: Stealing</div>
                <div className="text-text-dim ml-4">Blocked by: Captain, Ambassador</div>
              </div>
              <div>
                <RoleTag name="Ambassador" symbol="✦" />
                <div className="text-text-dim ml-4">Action: Exchange — draw 2 cards from the deck, return 2</div>
                <div className="text-text-dim ml-4">Blocks: Stealing</div>
              </div>
              <div>
                <RoleTag name="Contessa" symbol="♥" />
                <div className="text-text-dim ml-4">Action: None</div>
                <div className="text-text-dim ml-4">Blocks: Assassination</div>
              </div>
            </div>
          </div>
        </div>

        {/* Right column */}
        <div className="space-y-5">
          <div>
            <h2 className="text-text-bright text-base">GENERAL ACTIONS</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <div className="space-y-2 ml-2 text-text-default">
              <div><span className="text-text-bright">Income</span>{"        "}Take 1 coin. Cannot be blocked or challenged.</div>
              <div><span className="text-text-bright">Foreign Aid</span>{"   "}Take 2 coins. Can be blocked by Duke.</div>
              <div><span className="text-text-bright">Coup</span>{"          "}Pay 7 coins, target loses influence. Mandatory at 10+ coins.</div>
            </div>
          </div>

          <div>
            <h2 className="text-text-bright text-base">CHALLENGES</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <p className="text-text-default">When a player claims a role, any other player may challenge.</p>
            <div className="space-y-1 ml-2 mt-2 text-text-default">
              <div><span className="text-cursor">Bluffing?</span>{"   "}Challenger wins. The bluffer loses an influence card.</div>
              <div><span className="text-cursor">Truthful?</span>{"   "}Challenger loses. Claimant reveals, shuffles back, draws new.</div>
            </div>
          </div>

          <div>
            <h2 className="text-text-bright text-base">BLOCKING</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <p className="text-text-default">
              Some actions can be blocked by specific roles. The blocker claims to have that role.
              The original actor (or anyone) can then challenge the block.
            </p>
          </div>

          <div>
            <h2 className="text-text-bright text-base">STRATEGY TIPS</h2>
            <div className="border-t border-border-term mt-1 mb-2" />
            <div className="space-y-1 ml-2 text-text-dim">
              <div>- Bluffing Duke early is strong — Tax gives 3 coins and Duke blocks Foreign Aid.</div>
              <div>- Challenge more when you hold cards of the claimed type — fewer copies left.</div>
              <div>- At 7+ coins, Coup is often better than role actions — can{"'"}t be blocked.</div>
              <div>- Watch what others claim. If two players both claim Duke, one is likely bluffing.</div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
