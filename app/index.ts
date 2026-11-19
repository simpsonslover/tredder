// Entry point: polyfills must load before anything else (crypto/chain libraries
// need crypto.getRandomValues and Buffer), then Expo Router takes over.
import './src/crashCatcher'; // first: an error anywhere after this is shown, not a silent close
import 'react-native-get-random-values';
import { Buffer } from 'buffer';

(globalThis as unknown as { Buffer: typeof Buffer }).Buffer ??= Buffer;

import 'expo-router/entry';
