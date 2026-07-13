import { BadRequestException, Body, Controller, Logger, Post } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import {
  IsArray,
  IsBoolean,
  IsIn,
  IsObject,
  IsOptional,
  IsString,
  Matches,
} from 'class-validator';
import { EngineError } from '@thoth/shared';
import { CurrentUser, type AuthUser } from '../common/decorators';
import {
  InstrumentService,
  type InstrumentPriceDto,
  type InstrumentTermsheetDto,
} from './instrument.service';

class InstrumentDto implements InstrumentPriceDto {
  @IsString() workspaceId!: string;
  @IsIn(['ana', 'pde', 'mcl', 'mcl_gpu']) engine!: 'ana' | 'pde' | 'mcl' | 'mcl_gpu';
  @IsIn(['vanilla', 'barrier', 'variance_swap', 'autocallable', 'asian', 'ratchet', 'digital']) kind!: string;
  //! free-form instrument fields — the engine validates the actual kind, so any variation
  //! it accepts (barrier_type, monitoring, nominal, is_absolute_strike, …) flows through.
  @IsObject() instrument!: Record<string, unknown>;
  @IsArray() @IsString({ each: true }) indicators!: string[];
  @IsOptional() @IsString() currency?: string;
  @IsOptional() @Matches(/^\d{4}-\d{2}-\d{2}$/) today?: string;
  @IsOptional() @IsBoolean() live?: boolean;
  @IsOptional() @IsString() configName?: string;
  @IsOptional() @IsString() correlationName?: string;
}

class TermsheetDto implements InstrumentTermsheetDto {
  @IsString() workspaceId!: string;
  @IsIn(['vanilla', 'barrier', 'variance_swap', 'autocallable', 'asian', 'ratchet', 'digital']) kind!: string;
  //! free-form instrument fields — the engine validates the actual kind (see InstrumentDto)
  @IsObject() instrument!: Record<string, unknown>;
  @IsOptional() @Matches(/^\d{4}-\d{2}-\d{2}$/) today?: string;
  @IsOptional() @IsString() title?: string;
  @IsOptional() @IsString() issuer?: string;
}

@ApiTags('instrument')
@Controller('instrument')
export class InstrumentController {
  private readonly log = new Logger(InstrumentController.name);

  constructor(private readonly instrument: InstrumentService) {}

  //! Engine errors can embed internal object/task names; log the full detail server-side
  //! but return a generic 400 so we do not leak the book's internals to the client.
  private engineBadRequest(op: string, e: EngineError): BadRequestException {
    this.log.warn(`instrument ${op} rejected by engine: ${e.message}`);
    return new BadRequestException('the instrument could not be priced as specified');
  }

  //! Price one hand-entered instrument synchronously (premium + Greeks). The frontend's
  //! pricing panels and the monitoring blotter call this; `live: true` overlays live spots.
  @Post('price')
  async price(@Body() dto: InstrumentDto, @CurrentUser() user: AuthUser) {
    try {
      return await this.instrument.price(dto, user);
    } catch (e) {
      // an engine-reported failure (bad/unpriceable product) is the caller's fault → 400.
      if (e instanceof EngineError) throw this.engineBadRequest('price', e);
      throw e;
    }
  }

  //! Render one hand-entered instrument's termsheet (Markdown) — the engine's !termsheet
  //! documentation task over the same contract + workspace market objects. The frontend
  //! downloads the returned document as a .md file.
  @Post('termsheet')
  async termsheet(@Body() dto: TermsheetDto, @CurrentUser() user: AuthUser) {
    try {
      return await this.instrument.termsheet(dto, user);
    } catch (e) {
      if (e instanceof EngineError) throw this.engineBadRequest('termsheet', e);
      throw e;
    }
  }
}
