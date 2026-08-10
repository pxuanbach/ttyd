import { h, Component } from 'preact';
import { OpenTab, FileEntry } from './types';
import { FileApi } from './api';

interface Props {
    tabs: OpenTab[];
    activeTab: string | null;
    onSelectTab: (path: string) => void;
    onCloseTab: (path: string) => void;
    onTabContentChange: (path: string, content: string) => void;
    onOpenFile: (entry: FileEntry) => void;
}

interface State {
    editingTab: string | null;
    saveStatus: 'idle' | 'saving' | 'saved' | 'error';
    error: string | null;
}

export class FileEditor extends Component<Props, State> {
    constructor(props: Props) {
        super(props);
        this.state = {
            editingTab: null,
            saveStatus: 'idle',
            error: null,
        };
    }

    handleTabClick = (path: string) => {
        this.props.onSelectTab(path);
    };

    handleCloseTab = (e: MouseEvent, path: string) => {
        e.stopPropagation();
        e.preventDefault();
        this.props.onCloseTab(path);
    };

    handleContentChange = (path: string, content: string) => {
        this.props.onTabContentChange(path, content);
    };

    handleSave = async (path: string) => {
        const tab = this.props.tabs.find(t => t.path === path);
        if (!tab || !tab.isDirty) return;

        this.setState({ saveStatus: 'saving', error: null });

        try {
            await FileApi.writeFile(path, tab.content);
            this.setState({ saveStatus: 'saved' });
            // Reset status after a delay
            setTimeout(() => {
                this.setState({ saveStatus: 'idle' });
            }, 2000);
        } catch (err) {
            this.setState({
                saveStatus: 'error',
                error: err instanceof Error ? err.message : 'Save failed',
            });
        }
    };

    handleCreateFile = async () => {
        const name = prompt('Enter file name:');
        if (!name) return;

        try {
            await FileApi.writeFile(name, '');
            // Notify parent to refresh directory
            // For now, just open the new file
            const entry: FileEntry = {
                name,
                path: name,
                isDirectory: false,
                size: 0,
                modified: new Date().toISOString(),
            };
            this.props.onOpenFile(entry);
        } catch (err) {
            alert(`Failed to create file: ${err instanceof Error ? err.message : err}`);
        }
    };

    render() {
        const { tabs, activeTab } = this.props;
        const { saveStatus, error } = this.state;

        if (tabs.length === 0) {
            return (
                <div class="file-editor empty">
                    <div class="editor-empty-message">
                        <p>No files open</p>
                        <p class="hint">Double-click a file to open it</p>
                    </div>
                </div>
            );
        }

        return (
            <div class="file-editor">
                <div class="editor-tabs">
                    <div class="tabs-list">
                        {tabs.map(tab => (
                            <div
                                key={tab.path}
                                class={`editor-tab ${activeTab === tab.path ? 'active' : ''} ${tab.isDirty ? 'dirty' : ''}`}
                                onClick={() => this.handleTabClick(tab.path)}
                            >
                                <span class="tab-name">{tab.name}</span>
                                {tab.isDirty && <span class="tab-dirty">●</span>}
                                <button
                                    class="tab-close"
                                    onClick={e => this.handleCloseTab(e as unknown as MouseEvent, tab.path)}
                                    title="Close"
                                >
                                    ×
                                </button>
                            </div>
                        ))}
                    </div>
                    <div class="tabs-actions">
                        <button class="action-btn" onClick={this.handleCreateFile} title="New file">
                            + New
                        </button>
                    </div>
                </div>

                <div class="editor-content">
                    {tabs.map(tab => (
                        <div
                            key={tab.path}
                            class={`editor-panel ${activeTab === tab.path ? 'active' : ''}`}
                            style={{ display: activeTab === tab.path ? 'block' : 'none' }}
                        >
                            <div class="editor-toolbar">
                                <span class="editor-filename">{tab.path}</span>
                                <div class="editor-actions">
                                    {tab.isDirty && (
                                        <button
                                            class="save-btn"
                                            onClick={() => this.handleSave(tab.path)}
                                            disabled={saveStatus === 'saving'}
                                        >
                                            {saveStatus === 'saving' ? 'Saving...' : 'Save'}
                                        </button>
                                    )}
                                    {saveStatus === 'saved' && tab.isDirty === false && (
                                        <span class="save-status saved">Saved</span>
                                    )}
                                </div>
                            </div>
                            {error && <div class="editor-error">{error}</div>}
                            <textarea
                                class="editor-textarea"
                                value={tab.content}
                                onInput={e =>
                                    this.handleContentChange(tab.path, (e.target as HTMLTextAreaElement).value)
                                }
                                spellcheck={false}
                            />
                        </div>
                    ))}
                </div>
            </div>
        );
    }
}
