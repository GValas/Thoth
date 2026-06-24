import { SetMetadata, createParamDecorator, type ExecutionContext } from '@nestjs/common';
import type { UserRole } from '../persistence/entities';

//! Mark a route as not requiring authentication (the global JwtAuthGuard skips it).
export const IS_PUBLIC_KEY = 'isPublic';
export const Public = () => SetMetadata(IS_PUBLIC_KEY, true);

//! Restrict a route to the given roles (enforced by RolesGuard).
export const ROLES_KEY = 'roles';
export const Roles = (...roles: UserRole[]) => SetMetadata(ROLES_KEY, roles);

//! The authenticated principal attached to the request by JwtStrategy.
export interface AuthUser {
  userId: string;
  email: string;
  role: UserRole;
}

//! Inject the current user into a handler param.
export const CurrentUser = createParamDecorator(
  (_data: unknown, ctx: ExecutionContext): AuthUser => ctx.switchToHttp().getRequest().user,
);
