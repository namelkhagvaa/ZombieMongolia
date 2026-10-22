import { createContext, useContext } from 'react';
import type { StaffUser } from './api';

export const AuthContext = createContext<{ user: StaffUser | null }>({ user: null });

export function useAuth() {
  return useContext(AuthContext);
}
