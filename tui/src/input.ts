// T24: Input handler — global key dispatch

export type KeyCallback = (key: string) => void;

export class InputHandler {
  private handler: KeyCallback | null = null;
  private resolveAction: ((value: string) => void) | null = null;

  setHandler(handler: KeyCallback) {
    this.handler = handler;
  }

  clearHandler() {
    this.handler = null;
  }

  processKey(keyName: string) {
    if (this.resolveAction) {
      this.resolveAction(keyName);
      this.resolveAction = null;
      return;
    }
    if (this.handler) {
      this.handler(keyName);
    }
  }

  waitForKey(): Promise<string> {
    return new Promise((resolve) => {
      this.resolveAction = resolve;
    });
  }
}
