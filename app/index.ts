// Entry point: polyfills must load before anything else (crypto/chain libraries
// need crypto.getRandomValues and Buffer), then Expo Router takes over.
import 'react-native-get-random-values';
import { Buffer } from 'buffer';

(globalThis as unknown as { Buffer: typeof Buffer }).Buffer ??= Buffer;

import 'expo-router/entry';
