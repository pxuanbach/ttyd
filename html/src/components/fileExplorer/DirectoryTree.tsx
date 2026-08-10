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
        const parentPath = currentPath.split('/').slice(0, -1).join('/') || '/';
        this.loadDirectory(parentPath === '/' ? undefined : parentPath);
    };

    render() {
        const { entries, expandedPaths, selectedPath, currentPath, loading, error } = this.state;

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

                    {!loading && !error && entries.length === 0 && <div class="tree-empty">Empty directory</div>}

                    {!loading && !error && entries.length > 0 && (
                        <div class="tree-entries">
                            {entries.map(entry => {
                                // Build tree structure based on parent path
                                const relativePath = currentPath
                                    ? entry.path.replace(currentPath + '/', '')
                                    : entry.name;

                                if (relativePath.includes('/')) {
                                    // This is a nested entry, skip it (it will be rendered under its parent)
                                    return null;
                                }

                                return (
                                    <FileItem
                                        key={entry.path}
                                        entry={entry}
                                        depth={0}
                                        isExpanded={expandedPaths.has(entry.path)}
                                        isSelected={selectedPath === entry.path}
                                        onToggle={this.handleToggle}
                                        onSelect={this.handleSelect}
                                        onOpen={this.handleOpen}
                                    />
                                );
                            })}
                        </div>
                    )}
                </div>
            </div>
        );
    }
}
