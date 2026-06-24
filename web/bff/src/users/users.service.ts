import { Injectable, NotFoundException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import * as bcrypt from 'bcryptjs';
import { User, type UserRole } from '../persistence/entities';

//! User shape returned to the client — never the password / refresh hashes.
export interface PublicUser {
  id: string;
  email: string;
  role: UserRole;
  enabled: boolean;
  createdAt: Date;
}

function toPublic(u: User): PublicUser {
  return { id: u.id, email: u.email, role: u.role, enabled: u.enabled, createdAt: u.createdAt };
}

@Injectable()
export class UsersService {
  constructor(@InjectRepository(User) private readonly users: Repository<User>) {}

  async list(): Promise<PublicUser[]> {
    return (await this.users.find({ order: { createdAt: 'ASC' } })).map(toPublic);
  }

  async create(email: string, password: string, role: UserRole): Promise<PublicUser> {
    const user = this.users.create({ email, passwordHash: await bcrypt.hash(password, 10), role });
    return toPublic(await this.users.save(user));
  }

  async setEnabled(id: string, enabled: boolean): Promise<PublicUser> {
    return this.update(id, { enabled, ...(enabled ? {} : { refreshTokenHash: null }) });
  }

  async setRole(id: string, role: UserRole): Promise<PublicUser> {
    return this.update(id, { role });
  }

  async resetPassword(id: string, password: string): Promise<PublicUser> {
    return this.update(id, { passwordHash: await bcrypt.hash(password, 10), refreshTokenHash: null });
  }

  async remove(id: string): Promise<void> {
    const res = await this.users.delete(id);
    if (!res.affected) throw new NotFoundException('user not found');
  }

  private async update(id: string, patch: Partial<User>): Promise<PublicUser> {
    const user = await this.users.findOne({ where: { id } });
    if (!user) throw new NotFoundException('user not found');
    Object.assign(user, patch);
    return toPublic(await this.users.save(user));
  }
}
