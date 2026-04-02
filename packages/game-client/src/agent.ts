// T5: Agent interface + registry

export interface Agent {
  readonly name: string;
  chooseAction(obs: Float32Array, validMask: number): Promise<number>;
}

export type AgentFactory = () => Agent;

export class AgentRegistry {
  private factories = new Map<string, AgentFactory>();

  register(name: string, factory: AgentFactory) {
    this.factories.set(name, factory);
  }

  create(name: string): Agent {
    const factory = this.factories.get(name);
    if (!factory) {
      throw new Error(`Unknown agent type: ${name}. Available: ${[...this.factories.keys()].join(", ")}`);
    }
    return factory();
  }

  list(): string[] {
    return [...this.factories.keys()];
  }
}

export const registry = new AgentRegistry();

export type HumanActionCallback = (obs: Float32Array, validMask: number) => Promise<number>;

export class HumanAgent implements Agent {
  readonly name = "You";
  private callback: HumanActionCallback;

  constructor(callback: HumanActionCallback) {
    this.callback = callback;
  }

  async chooseAction(obs: Float32Array, validMask: number): Promise<number> {
    return this.callback(obs, validMask);
  }
}
