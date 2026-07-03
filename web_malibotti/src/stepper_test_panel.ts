import { LitElement, html } from "lit";
import type { PropertyValues } from "lit";
import { customElement, property, state } from "lit/decorators.js";
import type { IMessageSender } from "./app.js";

@customElement("stepper-test-panel")
export class StepperTestPanel extends LitElement {
	protected createRenderRoot() {
		return this;
	}

	@property({ type: Boolean }) accessor deviceConnected = false;
	@property() accessor messageSender: IMessageSender | undefined;

	@state() private accessor selectedStepper: 1 | 2 = 1;
	@state() private accessor stepCount = 512;
	@state() private accessor rpmX10 = 120;
	@state() private accessor pendingStepper1: number | null = null;
	@state() private accessor pendingStepper2: number | null = null;

	private unsubscribeMessages: (() => void) | null = null;

	private static readonly STEPPER_1_NAMESPACE = 0x0004;
	private static readonly STEPPER_2_NAMESPACE = 0x0005;
	private static readonly MESSAGE_ENQUEUE_TURN = 0x0001;
	private static readonly MESSAGE_GET_PENDING = 0x0002;

	connectedCallback() {
		super.connectedCallback();
		this.attachMessageSubscription();
	}

	disconnectedCallback() {
		this.detachMessageSubscription();
		super.disconnectedCallback();
	}

	protected updated(changedProperties: PropertyValues<this>) {
		if (changedProperties.has("messageSender")) {
			this.attachMessageSubscription();
		}
	}

	private encodeU16Le(value: number) {
		const clamped = Math.max(0, Math.min(0xffff, Math.round(value)));
		return [clamped & 0xff, (clamped >> 8) & 0xff];
	}

	private encodeI32Le(value: number) {
		const signed = Math.trunc(value) | 0;
		const unsigned = signed >>> 0;
		return [
			unsigned & 0xff,
			(unsigned >> 8) & 0xff,
			(unsigned >> 16) & 0xff,
			(unsigned >> 24) & 0xff,
		];
	}

	private getSelectedNamespace() {
		return this.selectedStepper === 1
			? StepperTestPanel.STEPPER_1_NAMESPACE
			: StepperTestPanel.STEPPER_2_NAMESPACE;
	}

	private decodeU16Le(payload: Uint8Array, offset: number) {
		const lo = payload[offset] ?? 0;
		const hi = payload[offset + 1] ?? 0;
		return lo | (hi << 8);
	}

	private attachMessageSubscription() {
		this.detachMessageSubscription();
		if (!this.messageSender) {
			return;
		}
		this.unsubscribeMessages = this.messageSender.subscribe((namespaceId, messageId, payload) => {
			if (messageId !== StepperTestPanel.MESSAGE_GET_PENDING) {
				return;
			}
			const pending = this.decodeU16Le(payload, 0);
			if (namespaceId === StepperTestPanel.STEPPER_1_NAMESPACE) {
				this.pendingStepper1 = pending;
				return;
			}
			if (namespaceId === StepperTestPanel.STEPPER_2_NAMESPACE) {
				this.pendingStepper2 = pending;
			}
		});
	}

	private detachMessageSubscription() {
		if (!this.unsubscribeMessages) {
			return;
		}
		this.unsubscribeMessages();
		this.unsubscribeMessages = null;
	}

	private async enqueueTurn(steps: number) {
		const payload = new Uint8Array([
			...this.encodeI32Le(steps),
			...this.encodeU16Le(this.rpmX10),
		]);
		await this.messageSender?.send(
			this.getSelectedNamespace(),
			StepperTestPanel.MESSAGE_ENQUEUE_TURN,
			payload
		);
	}

	private async requestPendingCount() {
		await this.messageSender?.send(
			this.getSelectedNamespace(),
			StepperTestPanel.MESSAGE_GET_PENDING,
			new Uint8Array(0)
		);
	}

	private onStepperChange(event: Event) {
		const target = event.target as HTMLSelectElement;
		this.selectedStepper = target.value === "2" ? 2 : 1;
	}

	private onStepCountInput(event: Event) {
		const target = event.target as HTMLInputElement;
		const parsed = Number.parseInt(target.value, 10);
		this.stepCount = Number.isNaN(parsed) ? 0 : Math.max(-2000000000, Math.min(2000000000, parsed));
	}

	private onRpmInput(event: Event) {
		const target = event.target as HTMLInputElement;
		const parsed = Number.parseFloat(target.value);
		const rpm = Number.isNaN(parsed) ? 0 : Math.max(0, parsed);
		this.rpmX10 = Math.round(rpm * 10);
	}

	private sendCustomTurn = () => {
		void this.enqueueTurn(this.stepCount);
	};

	private sendQuarterForward = () => {
		void this.enqueueTurn(512);
	};

	private sendQuarterBackward = () => {
		void this.enqueueTurn(-512);
	};

	private sendOneRevolutionForward = () => {
		void this.enqueueTurn(2048);
	};

	private sendOneRevolutionBackward = () => {
		void this.enqueueTurn(-2048);
	};

	private sendPendingRequest = () => {
		void this.requestPendingCount();
	};

	render() {
		const rpmDisplay = (this.rpmX10 / 10).toFixed(1);
		const selectedPending = this.selectedStepper === 1 ? this.pendingStepper1 : this.pendingStepper2;
		const selectedPendingLabel = selectedPending === null ? "noch keine Antwort" : String(selectedPending);
		const pending1Label = this.pendingStepper1 === null ? "-" : String(this.pendingStepper1);
		const pending2Label = this.pendingStepper2 === null ? "-" : String(this.pendingStepper2);
		return html`
			<div class="panel app-panel">
				<div class="panel-section">
					<div class="panel-label">Stepper Test</div>
					<div class="panel-controls">
						<div class="panel-row">
							<label for="stepper-select">Stepper</label>
							<select
								id="stepper-select"
								class="panel-input"
								@change=${this.onStepperChange}
								?disabled=${!this.deviceConnected}
							>
								<option value="1" ?selected=${this.selectedStepper === 1}>Stepper 1 (Namespace 0x0004)</option>
								<option value="2" ?selected=${this.selectedStepper === 2}>Stepper 2 (Namespace 0x0005)</option>
							</select>
						</div>

						<div class="panel-row">
							<label for="rpm-input">Geschwindigkeit (rpm)</label>
							<input
								id="rpm-input"
								type="number"
								class="panel-input"
								min="0"
								step="0.1"
								.value=${rpmDisplay}
								@input=${this.onRpmInput}
								?disabled=${!this.deviceConnected}
							/>
						</div>

						<div class="panel-row">
							<label for="steps-input">Schritte (positiv/negativ)</label>
							<input
								id="steps-input"
								type="number"
								class="panel-input"
								step="1"
								.value=${String(this.stepCount)}
								@input=${this.onStepCountInput}
								?disabled=${!this.deviceConnected}
							/>
						</div>

						<div class="panel-button-grid">
							<button type="button" class="panel-choice-btn" @click=${this.sendQuarterForward} ?disabled=${!this.deviceConnected}>
								+1/4 Umdrehung
							</button>
							<button type="button" class="panel-choice-btn" @click=${this.sendQuarterBackward} ?disabled=${!this.deviceConnected}>
								-1/4 Umdrehung
							</button>
							<button type="button" class="panel-choice-btn" @click=${this.sendOneRevolutionForward} ?disabled=${!this.deviceConnected}>
								+1 Umdrehung
							</button>
							<button type="button" class="panel-choice-btn" @click=${this.sendOneRevolutionBackward} ?disabled=${!this.deviceConnected}>
								-1 Umdrehung
							</button>
						</div>

						<button type="button" class="panel-choice-btn" @click=${this.sendCustomTurn} ?disabled=${!this.deviceConnected}>
							Custom-Drehauftrag senden
						</button>

						<button type="button" class="panel-choice-btn" @click=${this.sendPendingRequest} ?disabled=${!this.deviceConnected}>
							Offene Auftraege abfragen
						</button>

						<div class="panel-value-row">
							<span>Aktueller Pending-Wert (ausgewaehlter Stepper)</span>
							<span class="panel-value-current">${selectedPendingLabel}</span>
						</div>
						<div class="panel-value-row">
							<span>Letzte Werte: Stepper 1 / Stepper 2</span>
							<span class="panel-value-current">${pending1Label} / ${pending2Label}</span>
						</div>

						<div class="panel-text">
							Bei laufendem Auftrag und leerer Queue liefert die Firmware den Wert 1.
						</div>
					</div>
				</div>
			</div>
		`;
	}
}

declare global {
	interface HTMLElementTagNameMap {
		"stepper-test-panel": StepperTestPanel;
	}
}
