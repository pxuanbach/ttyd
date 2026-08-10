import { h, Component } from 'preact';
import { FileEntry } from './types';
import { FileApi } from './api';
import { FileItem } from './FileItem';

interface Props {
    onOpenFile: (entry: FileEntry) => void;
}

interface State {
    entries: FileEntry[];
    expandedPaths: Set<string>;
    selectedPath: string | null;
    currentPath: string;
    loading: boolean;
    error: string | null;
}

export class DirectoryTree extends Component<Props, State> {
    constructor(props: Props) {
        super(props);
        this.state = {
            entries: [],
            expandedPaths: new Set(),
            selectedPath: null,
            currentPath: '',
            loading: true,
            error: null,
        };
    }

    async componentDidMount() {
        await this.loadDirectory();
    }

    loadDirectory = async (path?: string) => {
        this.setState({ loading: true, error: null });
        try {
            const result = await FileApi.listDirectory(path);
            this.setState(() => ({
                entries: result.entries,
                currentPath: result.path,
                loading: false,
            }));
        } catch (err) {
            this.setState({
                error: err instanceof Error ? err.message : 'Failed to load directory',
                loading: false,
            });
        }
    };

    handleToggle = async (entry: FileEntry) => {
        const { expandedPaths } = this.state;
        const newExpanded = new Set(expandedPaths);

        if (expandedPaths.has(entry.path)) {
            newExpanded.delete(entry.path);
            this.setState({ expandedPaths: newExpanded });
        } else {
            newExpanded.add(entry.path);
            this.setState({ expandedPaths: newExpanded });
            // Load subdirectory entries
            await this.loadSubdirectory(entry.path);
        }
    };

    loadSubdirectory = async (path: string) => {
        try {
            const result = await FileApi.listDirectory(path);
            this.setState(_prev => {
                // Merge subdirectory entries into main entries
                const existingPaths = new Set(_prev.entries.map(e => e.path));
                const newEntries = result.entries.filter(e => !existingPaths.has(e.path));
                return {
                    entries: [..._prev.entries, ...newEntries],
                };
            });
        } catch (err) {
            console.error('Failed to load subdirectory:', err);
        }
    };

    handleSelect = (entry: FileEntry) => {
        this.setState({ selectedPath: entry.path });
    };

    handleOpen = async (entry: FileEntry) => {
        if (!entry.isDirectory) {
            this.props.onOpenFile(entry);
        }
    };

    reload = () => {
        const { currentPath } = this.state;
        this.loadDirectory(currentPath || undefined);
    };

    navigateUp = () => {
        const { currentPath } = this.state;
        // Normalize path - replace backslashes with forward slashes, remove trailing slashes
        const normalizedPath = currentPath.split('\\').join('/').replace(/\/+$/, '');
        const parts = normalizedPath.split('/');

        if (parts.length <= 1 || normalizedPath === '') {
            // Already at root
            return;
        }

        parts.pop();
        const parentPath = parts.join('/') || '';
        this.loadDirectory(parentPath || undefined);
    };

    getChildEntries = (parentPath: string): FileEntry[] => {
        const { entries } = this.state;
        const normalizedParent = parentPath.split('\\').join('/').replace(/\/+$/, '');

        return entries.filter(entry => {
            const normalizedEntry = entry.path.split('\\').join('/').replace(/\/+$/, '');
            const entryDir = normalizedEntry.substring(0, normalizedEntry.lastIndexOf('/'));
            return entryDir === normalizedParent;
        });
    };

    renderEntry = (entry: FileEntry, depth: number) => {
        const { expandedPaths, selectedPath } = this.state;
        const isExpanded = expandedPaths.has(entry.path);

        return (
            <div key={entry.path}>
                <FileItem
                    entry={entry}
                    depth={depth}
                    isExpanded={isExpanded}
                    isSelected={selectedPath === entry.path}
                    onToggle={this.handleToggle}
                    onSelect={this.handleSelect}
                    onOpen={this.handleOpen}
                />
                {entry.isDirectory && isExpanded && (
                    <div class="tree-children">
                        {this.getChildEntries(entry.path).map(child => this.renderEntry(child, depth + 1))}
                    </div>
                )}
            </div>
        );
    };

    render() {
        const { entries, currentPath, loading, error } = this.state;

        // Get root entries (direct children of current path)
        const normalizedCurrentPath = currentPath.replace(/\\/g, '/').replace(/\/+$/, '');
        const rootEntries = entries.filter(entry => {
            const normalizedEntry = entry.path.replace(/\\/g, '/').replace(/\/+$/, '');
            const entryDir = normalizedEntry.substring(0, normalizedEntry.lastIndexOf('/'));
            return entryDir === normalizedCurrentPath;
        });

        return (
            <div class="directory-tree">
                <div class="tree-header">
                    <button class="tree-btn" onClick={this.navigateUp} title="Go up">
                        ⬆️
                    </button>
                    <button class="tree-btn" onClick={this.reload} title="Reload">
                        🔄
                    </button>
                    <span class="tree-path" title={currentPath}>
                        {currentPath || '/'}
                    </span>
                </div>

                <div class="tree-content">
                    {loading && (
                        <div class="tree-loading">
                            <span>Loading...</span>
                        </div>
                    )}

                    {error && (
                        <div class="tree-error">
                            <span>Error: {error}</span>
                            <button onClick={this.reload}>Retry</button>
                        </div>
                    )}

                    {!loading && !error && rootEntries.length === 0 && <div class="tree-empty">Empty directory</div>}

                    {!loading && !error && rootEntries.length > 0 && (
                        <div class="tree-entries">{rootEntries.map(entry => this.renderEntry(entry, 0))}</div>
                    )}
                </div>
            </div>
        );
    }
}
