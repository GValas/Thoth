import { Injectable, NotFoundException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Workspace } from '../persistence/entities';

@Injectable()
export class WorkspacesService {
  constructor(@InjectRepository(Workspace) private readonly repo: Repository<Workspace>) {}

  //! List only the caller's own workspaces (admins see all). Owner-scoping here is the
  //! single choke point every workspace-derived resource (market data, grids, pricing)
  //! authorizes through, so IDOR cannot leak another user's book by guessing its id.
  list(userId: string, isAdmin = false): Promise<Workspace[]> {
    return this.repo.find({
      where: isAdmin ? {} : { ownerId: userId },
      order: { createdAt: 'ASC' },
    });
  }

  //! Resolve a workspace AND authorize it for the caller in one step. A workspace owned by
  //! someone else is reported as 404 (not 403) on purpose: a 403 would confirm the id exists
  //! and enable enumeration of other users' workspace ids. Admins bypass the ownership check.
  async get(id: string, userId: string, isAdmin = false): Promise<Workspace> {
    const ws = await this.repo.findOne({ where: { id } });
    if (!ws || (!isAdmin && ws.ownerId !== userId)) {
      throw new NotFoundException('workspace not found');
    }
    return ws;
  }

  create(data: Pick<Workspace, 'name'> & Partial<Workspace>): Promise<Workspace> {
    return this.repo.save(this.repo.create(data));
  }

  async update(id: string, patch: Partial<Workspace>, userId: string, isAdmin = false): Promise<Workspace> {
    const ws = await this.get(id, userId, isAdmin);
    Object.assign(ws, patch);
    return this.repo.save(ws);
  }
}
