import { h, Component } from 'preact';

export interface ContextMenuItem {
    id: string;
    label: string;
    icon?: string;
    disabled?: boolean;
    divider?: boolean;
}

interface Props {
    x: number;
    y: number;
    items: ContextMenuItem[];
    onSelect: (id: string) => void;
    onClose: () => void;
}

interface State {
    adjustedX: number;
    adjustedY: number;
}

export class ContextMenu extends Component<Props, State> {
    private menuRef: HTMLDivElement | null = null;

    constructor(props: Props) {
        super(props);
        this.state = {
            adjustedX: props.x,
            adjustedY: props.y,
        };
    }

    componentDidMount() {
        // Adjust position if menu would overflow viewport
        this.adjustPosition();

        // Add click outside listener
        document.addEventListener('click', this.handleClickOutside);
        document.addEventListener('contextmenu', this.handleClickOutside);

        // Add escape key listener
        document.addEventListener('keydown', this.handleEscape);
    }

    componentWillUnmount() {
        document.removeEventListener('click', this.handleClickOutside);
        document.removeEventListener('contextmenu', this.handleClickOutside);
        document.removeEventListener('keydown', this.handleEscape);
    }

    adjustPosition = () => {
        if (!this.menuRef) return;

        const menuRect = this.menuRef.getBoundingClientRect();
        const viewportWidth = window.innerWidth;
        const viewportHeight = window.innerHeight;

        let { x, y } = this.props;

        // Adjust horizontal position
        if (x + menuRect.width > viewportWidth) {
            x = viewportWidth - menuRect.width - 10;
        }
        if (x < 10) x = 10;

        // Adjust vertical position
        if (y + menuRect.height > viewportHeight) {
            y = viewportHeight - menuRect.height - 10;
        }
        if (y < 10) y = 10;

        if (x !== this.props.x || y !== this.props.y) {
            this.setState({ adjustedX: x, adjustedY: y });
        }
    };

    handleClickOutside = (e: MouseEvent) => {
        if (this.menuRef && !this.menuRef.contains(e.target as Node)) {
            e.preventDefault();
            e.stopPropagation();
            this.props.onClose();
        }
    };

    handleEscape = (e: KeyboardEvent) => {
        if (e.key === 'Escape') {
            this.props.onClose();
        }
    };

    handleSelect = (id: string, disabled?: boolean) => {
        if (!disabled) {
            this.props.onSelect(id);
        }
    };

    render() {
        const { items } = this.props;
        const { adjustedX, adjustedY } = this.state;

        return (
            <div
                ref={ref => {
                    this.menuRef = ref;
                }}
                class="context-menu"
                style={{ left: `${adjustedX}px`, top: `${adjustedY}px` }}
            >
                {items.map(item => {
                    if (item.divider) {
                        return <div class="context-menu__divider" key={item.id} />;
                    }

                    return (
                        <div
                            key={item.id}
                            class={`context-menu__item ${item.disabled ? 'context-menu__item--disabled' : ''}`}
                            onClick={() => this.handleSelect(item.id, item.disabled)}
                        >
                            {item.icon && <span class="context-menu__icon">{item.icon}</span>}
                            <span class="context-menu__label">{item.label}</span>
                        </div>
                    );
                })}
            </div>
        );
    }
}
