import { h, Component } from 'preact';

interface Props {
    imageUrl: string;
    fileName: string;
    onClose: () => void;
}

export class ImagePreviewModal extends Component<Props> {
    componentDidMount() {
        document.addEventListener('keydown', this.handleKeyDown);
    }

    componentWillUnmount() {
        document.removeEventListener('keydown', this.handleKeyDown);
    }

    handleKeyDown = (e: KeyboardEvent) => {
        if (e.key === 'Escape') {
            this.props.onClose();
        }
    };

    handleBackdropClick = (e: MouseEvent) => {
        // Close only when the backdrop itself is clicked, not the image or close button.
        if (e.target === e.currentTarget) {
            this.props.onClose();
        }
    };

    render() {
        const { imageUrl, fileName, onClose } = this.props;
        return (
            <div class="image-modal" onClick={this.handleBackdropClick}>
                <div class="image-modal-header">
                    <span class="image-modal-title">{fileName}</span>
                    <button class="image-modal-close" onClick={onClose} title="Close (Esc)" aria-label="Close">
                        ×
                    </button>
                </div>
                <div class="image-modal-body">
                    <img class="image-modal-img" src={imageUrl} alt={fileName} draggable={false} />
                </div>
            </div>
        );
    }
}
