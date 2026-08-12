export interface FileEntry {
    name: string;
    path: string;
    isDirectory: boolean;
    size: number;
    modified: string;
}

export interface DirectoryResult {
    path: string;
    entries: FileEntry[];
}

export interface FileResult {
    path: string;
    size: number;
    content: string;
}

export interface FileError {
    error: string;
}

export interface WriteResult {
    success?: boolean;
    error?: string;
}

export interface UploadResult {
    success: boolean;
    path?: string;
    size?: number;
    error?: string;
}

export interface UploadProgress {
    loaded: number;
    total: number;
    percent: number;
}

export type TabKind = 'text' | 'image';

export interface OpenTab {
    path: string;
    name: string;
    content: string;
    originalContent: string;
    isDirty: boolean;
    kind: TabKind;
}

// Extensions the backend /api/image endpoint can preview (raster only).
export const IMAGE_EXTENSIONS = ['png', 'jpg', 'jpeg', 'gif', 'webp'] as const;

export function isImageFile(name: string): boolean {
    const dot = name.lastIndexOf('.');
    if (dot < 0 || dot === name.length - 1) return false;
    const ext = name.slice(dot + 1).toLowerCase();
    return (IMAGE_EXTENSIONS as readonly string[]).includes(ext);
}

export function isDirectoryResult(obj: unknown): obj is DirectoryResult {
    return typeof obj === 'object' && obj !== null && 'path' in obj && 'entries' in obj;
}

export function isFileResult(obj: unknown): obj is FileResult {
    return typeof obj === 'object' && obj !== null && 'path' in obj && 'content' in obj;
}

export function isFileError(obj: unknown): obj is FileError {
    return typeof obj === 'object' && obj !== null && 'error' in obj;
}
