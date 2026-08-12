import { h, Component } from 'preact';
import { FileEntry } from './types';

interface Props {
    entry: FileEntry;
    depth: number;
    isExpanded: boolean;
    isSelected: boolean;
    onToggle: (entry: FileEntry) => void;
    onSelect: (entry: FileEntry) => void;
    onOpen: (entry: FileEntry) => void;
    onContextMenu?: (entry: FileEntry, x: number, y: number) => void;
}

export class FileItem extends Component<Props> {
    handleClick = (e: MouseEvent) => {
        e.stopPropagation();
        const { entry, onSelect, onToggle, onOpen } = this.props;

        onSelect(entry);

        if (entry.isDirectory) {
            onToggle(entry);
        } else {
            onOpen(entry);
        }
    };

    handleDoubleClick = (e: MouseEvent) => {
        e.stopPropagation();
        const { entry, onOpen } = this.props;
        if (!entry.isDirectory) {
            onOpen(entry);
        }
    };

    handleContextMenu = (e: MouseEvent) => {
        e.preventDefault();
        e.stopPropagation();
        const { entry, onContextMenu } = this.props;
        if (onContextMenu) {
            onContextMenu(entry, e.clientX, e.clientY);
        }
    };

    render() {
        const { entry, depth, isExpanded, isSelected } = this.props;
        const paddingLeft = depth * 16 + 8;

        const icon = entry.isDirectory ? (isExpanded ? '📂' : '📁') : getFileIcon(entry.name);

        return (
            <div
                class={`file-item ${isSelected ? 'selected' : ''}`}
                style={{ paddingLeft: `${paddingLeft}px` }}
                onClick={this.handleClick}
                onDblClick={this.handleDoubleClick}
                onContextMenu={this.handleContextMenu}
                title={entry.path}
            >
                <span class="file-icon">{icon}</span>
                <span class="file-name">{entry.name}</span>
                {!entry.isDirectory && <span class="file-size">{formatFileSize(entry.size)}</span>}
            </div>
        );
    }
}

function getFileIcon(filename: string): string {
    const ext = filename.split('.').pop()?.toLowerCase() || '';
    const icons: Record<string, string> = {
        // Text files
        txt: '📄',
        md: '📝',
        // Code files
        js: '📜',
        ts: '📘',
        tsx: '📘',
        jsx: '📜',
        py: '🐍',
        rb: '💎',
        go: '🔵',
        rs: '🦀',
        java: '☕',
        c: '⚙️',
        cpp: '⚙️',
        h: '⚙️',
        hpp: '⚙️',
        cs: '🔷',
        php: '🐘',
        swift: '🍎',
        kt: '🟣',
        scala: '🔴',
        // Config files
        json: '📋',
        xml: '📋',
        yaml: '📋',
        yml: '📋',
        toml: '📋',
        ini: '📋',
        conf: '📋',
        config: '📋',
        // Web files
        html: '🌐',
        htm: '🌐',
        css: '🎨',
        scss: '🎨',
        sass: '🎨',
        less: '🎨',
        // Shell
        sh: '🐚',
        bash: '🐚',
        zsh: '🐚',
        fish: '🐟',
        // Image files
        png: '🖼️',
        jpg: '🖼️',
        jpeg: '🖼️',
        gif: '🖼️',
        svg: '🖼️',
        ico: '🖼️',
        webp: '🖼️',
        // Archive files
        zip: '📦',
        tar: '📦',
        gz: '📦',
        rar: '📦',
        '7z': '📦',
        // Other
        pdf: '📕',
        doc: '📘',
        docx: '📘',
        xls: '📗',
        xlsx: '📗',
        ppt: '📙',
        pptx: '📙',
        exe: '⚡',
        dll: '⚙️',
        so: '⚙️',
        dylib: '⚙️',
    };
    return icons[ext] || '📄';
}

function formatFileSize(bytes: number): string {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(1))} ${sizes[i]}`;
}
