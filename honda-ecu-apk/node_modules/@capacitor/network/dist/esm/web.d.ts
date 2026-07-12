import { WebPlugin } from '@capacitor/core';
import type { ConnectionStatus, NetworkPlugin } from './definitions';
declare global {
    interface Navigator {
        connection: any;
        mozConnection: any;
        webkitConnection: any;
    }
}
export declare class NetworkWeb extends WebPlugin implements NetworkPlugin {
    constructor();
    getStatus(): Promise<ConnectionStatus>;
    private handleOnline;
    private handleOffline;
}
declare const Network: NetworkWeb;
export { Network };
