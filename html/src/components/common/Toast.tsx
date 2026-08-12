import { h, Component } from 'preact';
import { createPortal } from 'preact/compat';

export interface ToastMessage {
    id: number;
    message: string;
    type: 'success' | 'error' | 'warning' | 'info';
}

interface Props {}

interface State {
    toasts: ToastMessage[];
}

let toastId = 0;

export function showToast(message: string, type: ToastMessage['type'] = 'info') {
    const event = new CustomEvent('showToast', {
        detail: { message, type },
    });
    window.dispatchEvent(event);
}

export const toast = {
    success: (message: string) => showToast(message, 'success'),
    error: (message: string) => showToast(message, 'error'),
    warning: (message: string) => showToast(message, 'warning'),
    info: (message: string) => showToast(message, 'info'),
};

export class ToastContainer extends Component<Props, State> {
    private containerRef: HTMLDivElement | null = null;

    constructor(props: Props) {
        super(props);
        this.state = {
            toasts: [],
        };
    }

    componentDidMount() {
        window.addEventListener('showToast', this.handleShowToast as EventListener);
    }

    componentWillUnmount() {
        window.removeEventListener('showToast', this.handleShowToast as EventListener);
    }

    handleShowToast = (e: Event) => {
        const { message, type } = (e as CustomEvent).detail as { message: string; type: ToastMessage['type'] };
        const id = ++toastId;

        this.setState(prev => ({
            toasts: [...prev.toasts, { id, message, type }],
        }));

        // Auto-dismiss after 3 seconds
        setTimeout(() => {
            this.dismissToast(id);
        }, 3000);
    };

    dismissToast = (id: number) => {
        this.setState(prev => ({
            toasts: prev.toasts.filter(t => t.id !== id),
        }));
    };

    getIcon(type: ToastMessage['type']): string {
        switch (type) {
            case 'success':
                return '✓';
            case 'error':
                return '✕';
            case 'warning':
                return '⚠';
            case 'info':
            default:
                return 'ℹ';
        }
    }

    render() {
        const { toasts } = this.state;

        return createPortal(
            <div
                class="toast-container"
                ref={ref => {
                    this.containerRef = ref;
                }}
            >
                {toasts.map(t => (
                    <div key={t.id} class={`toast toast--${t.type}`}>
                        <span class="toast__icon">{this.getIcon(t.type)}</span>
                        <span class="toast__message">{t.message}</span>
                        <button class="toast__close" onClick={() => this.dismissToast(t.id)}>
                            ✕
                        </button>
                    </div>
                ))}
            </div>,
            document.body
        );
    }
}
