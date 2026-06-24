import { Controller, Get } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import { Public } from './common/decorators';
import { EngineService } from './engine/engine.service';

//! Public liveness/readiness: reports how many engine replicas are reachable.
@ApiTags('health')
@Controller('health')
export class HealthController {
  constructor(private readonly engine: EngineService) {}

  @Public()
  @Get()
  async health() {
    return { status: 'ok', engineReplicas: this.engine.size, healthy: await this.engine.healthyReplicas() };
  }
}
