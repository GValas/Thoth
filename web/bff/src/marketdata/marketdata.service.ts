//! Market-data objects of a workspace: store/replace them (validated), and materialise
//! a tagged YAML book from them for pricing. The materialised book reuses @thoth/shared
//! so the engine sees exactly the tags it expects.

import { BadRequestException, Injectable } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { buildBookYaml } from '@thoth/shared';
import { ObjectEntity } from '../persistence/entities';
import { SchemaService } from '../schema/schema.service';
import { WorkspacesService } from '../workspaces/workspaces.service';
import { validateObjects, type WsObject } from '../common/semantic-validation';
import { generateMarketData, type SeedOptions } from './seed-generator';

@Injectable()
export class MarketDataService {
  constructor(
    @InjectRepository(ObjectEntity) private readonly objects: Repository<ObjectEntity>,
    private readonly workspaces: WorkspacesService,
    private readonly schema: SchemaService,
  ) {}

  //! Public (controller) entry: authorize the workspace for the caller first (404 if it is
  //! not theirs — see WorkspacesService.get), then read its objects. The unscoped internal
  //! reader below is only reached AFTER the caller has been authorized on the workspace.
  async listObjectsFor(workspaceId: string, userId: string, isAdmin: boolean): Promise<WsObject[]> {
    await this.workspaces.get(workspaceId, userId, isAdmin);
    return this.listObjects(workspaceId);
  }

  //! Internal, id-only reader — callers (GridService/InstrumentService) MUST have already
  //! authorized the workspace through WorkspacesService.get before invoking this.
  async listObjects(workspaceId: string): Promise<WsObject[]> {
    const rows = await this.objects.find({ where: { workspaceId }, order: { name: 'ASC' } });
    return rows.map((o) => ({ name: o.name, kind: o.kind, payload: o.payload }));
  }

  //! Authorize then replace: the workspace must belong to the caller (or admin).
  async replaceObjectsFor(
    workspaceId: string,
    objects: WsObject[],
    userId: string,
    isAdmin: boolean,
  ): Promise<WsObject[]> {
    await this.workspaces.get(workspaceId, userId, isAdmin);
    return this.replaceObjects(workspaceId, objects);
  }

  //! Validate the proposed object set (ajv + semantic), then replace the workspace's
  //! objects transactionally. Rejects with the per-object error map on any problem.
  async replaceObjects(workspaceId: string, objects: WsObject[]): Promise<WsObject[]> {
    const errors = validateObjects(this.schema, objects);
    if (Object.keys(errors).length > 0) {
      throw new BadRequestException({ message: 'validation failed', errors });
    }
    await this.objects.manager.transaction(async (tx) => {
      await tx.delete(ObjectEntity, { workspaceId });
      await tx.save(
        ObjectEntity,
        objects.map((o) => tx.create(ObjectEntity, { workspaceId, ...o })),
      );
    });
    return this.listObjects(workspaceId);
  }

  //! Validate without persisting (the UI "check" button).
  validate(objects: WsObject[]): Record<string, string[]> {
    return validateObjects(this.schema, objects);
  }

  //! Generate a coherent random market-data set (equities + currencies + fx + correlation)
  //! using the workspace's valuation date, then REPLACE the workspace's objects with it
  //! (a fresh sample book). Goes through the same validate+persist path as replaceObjects.
  async seed(
    workspaceId: string,
    opts: SeedOptions = {},
    userId: string,
    isAdmin: boolean,
  ): Promise<WsObject[]> {
    //! authorize + resolve the workspace (404 if not the caller's) before generating/persisting.
    const ws = await this.workspaces.get(workspaceId, userId, isAdmin);
    const objects = generateMarketData({ ...opts, today: opts.today ?? ws.today });
    return this.replaceObjects(workspaceId, objects);
  }

  //! Build a tagged YAML book of all the workspace's objects plus the extra objects the
  //! caller adds (the grid pricer + cells). `rootTask` is the pricer name to run.
  async materializeBookYaml(
    workspaceId: string,
    rootTask: string,
    extra: WsObject[] = [],
  ): Promise<string> {
    const objs = [...(await this.listObjects(workspaceId)), ...extra];
    return buildBookYaml(rootTask, objs, this.schema.kinds());
  }
}
