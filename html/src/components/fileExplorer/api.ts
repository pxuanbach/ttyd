import { DirectoryResult, FileResult, WriteResult, isFileError } from './types';

const basePath = window.location.pathname.replace(/[/]+$/, '');

export class FileApi {
    private static async request<T>(endpoint: string, method: string = 'GET', body?: object): Promise<T> {
        const url = `${basePath}${endpoint}`;
        const options: RequestInit = {
            method,
            headers: {
                'Content-Type': 'application/json',
            },
        };

        if (body) {
            options.body = JSON.stringify(body);
        }

        const response = await fetch(url, options);
        const data = await response.json();

        if (!response.ok || isFileError(data)) {
            throw new Error(isFileError(data) ? data.error : `HTTP ${response.status}`);
        }

        return data as T;
    }

    static async listDirectory(path?: string): Promise<DirectoryResult> {
        const query = path ? `?path=${encodeURIComponent(path)}` : '';
        return this.request<DirectoryResult>(`/api/directory${query}`);
    }

    static async readFile(path: string): Promise<FileResult> {
        const query = `?path=${encodeURIComponent(path)}`;
        return this.request<FileResult>(`/api/file${query}`);
    }

    static async writeFile(path: string, content: string): Promise<WriteResult> {
        return this.request<WriteResult>('/api/file', 'POST', { path, content });
    }

    static async deleteFile(path: string): Promise<WriteResult> {
        const query = `?path=${encodeURIComponent(path)}`;
        return this.request<WriteResult>(`/api/file${query}`, 'DELETE');
    }
}
